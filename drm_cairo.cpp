#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <cairo/cairo.h>
#include <cstring>
#include <vector>

struct Buffer {
    uint32_t handle, pitch, size, fb_id;
    uint8_t *map;
};

// Helper to get property IDs and values
uint32_t get_prop_id(int fd, uint32_t obj_id, uint32_t obj_type, const char *name, uint64_t *val_out = nullptr) {
    drmModeObjectProperties *props = drmModeObjectGetProperties(fd, obj_id, obj_type);
    if (!props) return 0;
    uint32_t id = 0;
    for (uint32_t i = 0; i < props->count_props; i++) {
        drmModePropertyRes *prop = drmModeGetProperty(fd, props->props[i]);
        if (prop && strcmp(prop->name, name) == 0) {
            id = prop->prop_id;
            if (val_out) *val_out = props->prop_values[i];
        }
        drmModeFreeProperty(prop);
        if (id != 0) break;
    }
    drmModeFreeObjectProperties(props);
    return id;
}

int main() {
    int fd = open("/dev/dri/card1", O_RDWR | O_CLOEXEC);
    if (fd < 0) fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (fd < 0) { perror("Open DRM failed"); return 1; }

    drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
    drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1);

    drmModeRes *res = drmModeGetResources(fd);
    drmModeConnector *conn = nullptr;
    for (int i = 0; i < res->count_connectors; i++) {
        conn = drmModeGetConnector(fd, res->connectors[i]);
        if (conn && conn->connection == DRM_MODE_CONNECTED) {
            std::cout << "--- Connector Info ---" << std::endl;
            std::cout << "ID: " << conn->connector_id << " | Type: " << conn->connector_type << std::endl;
            break;
        }
        drmModeFreeConnector(conn);
        conn = nullptr;
    }
    if (!conn) { std::cerr << "No monitor found." << std::endl; return 1; }

    drmModeModeInfo mode = conn->modes[0];
    drmModeEncoder *enc = drmModeGetEncoder(fd, conn->encoder_id);
    uint32_t crtc_id = enc->crtc_id;
    int crtc_index = -1;
    for (int i = 0; i < res->count_crtcs; i++) {
        if (res->crtcs[i] == crtc_id) { crtc_index = i; break; }
    }
    std::cout << "Using CRTC ID: " << crtc_id << " (Index: " << crtc_index << ")" << std::endl;

    // --- PLANE DIAGNOSTICS ---
    uint32_t plane_id = 0;
    drmModePlaneRes *plane_res = drmModeGetPlaneResources(fd);
    std::cout << "--- Available Planes: " << plane_res->count_planes << " ---" << std::endl;
    
    for (uint32_t i = 0; i < plane_res->count_planes; i++) {
        drmModePlane *p = drmModeGetPlane(fd, plane_res->planes[i]);
        uint64_t type;
        get_prop_id(fd, p->plane_id, DRM_MODE_OBJECT_PLANE, "type", &type);
        
        const char* type_str = (type == DRM_PLANE_TYPE_PRIMARY) ? "PRIMARY" : 
                               (type == DRM_PLANE_TYPE_OVERLAY) ? "OVERLAY" : "CURSOR";

        std::cout << "Plane [" << i << "] ID: " << p->plane_id 
                  << " | Type: " << type_str 
                  << " | CRTC Mask: 0x" << std::hex << p->possible_crtcs << std::dec;

        // Pick the first Primary plane that matches our CRTC index
        if (plane_id == 0 && type == DRM_PLANE_TYPE_PRIMARY && (p->possible_crtcs & (1 << crtc_index))) {
            plane_id = p->plane_id;
            std::cout << "  <-- SELECTED";
        }
        std::cout << std::endl;
        drmModeFreePlane(p);
    }

    if (plane_id == 0) { std::cerr << "No compatible Primary plane found!" << std::endl; return 1; }

    // Buffer creation
    struct drm_mode_create_dumb creq = { .height = mode.vdisplay, .width = mode.hdisplay, .bpp = 32 };
    drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq);
    Buffer buf = { .handle = creq.handle, .pitch = creq.pitch, .size = creq.size };
    drmModeAddFB(fd, mode.hdisplay, mode.vdisplay, 24, 32, buf.pitch, buf.handle, &buf.fb_id);
    struct drm_mode_map_dumb mreq = { .handle = buf.handle };
    drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq);
    buf.map = (uint8_t *)mmap(0, buf.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, mreq.offset);

    // Cairo Draw
    cairo_surface_t *surf = cairo_image_surface_create_for_data(buf.map, CAIRO_FORMAT_ARGB32, mode.hdisplay, mode.vdisplay, buf.pitch);
    cairo_t *cr = cairo_create(surf);
    cairo_set_source_rgb(cr, 0.1, 0.4, 0.1); cairo_paint(cr); // Dark Green
    cairo_set_source_rgb(cr, 1, 1, 1); cairo_set_font_size(cr, 60);
    cairo_move_to(cr, 100, 100); cairo_show_text(cr, "Atomic Pipeline OK");

    // --- ATOMIC COMMIT ---
    drmModeAtomicReq *req = drmModeAtomicAlloc();
    auto add_p = [&](uint32_t obj_id, uint32_t obj_type, const char* name, uint64_t val) {
        uint32_t p_id = get_prop_id(fd, obj_id, obj_type, name);
        if (p_id > 0) {
            int r = drmModeAtomicAddProperty(req, obj_id, p_id, val);
            return r >= 0;
        }
        return false;
    };

    add_p(plane_id, DRM_MODE_OBJECT_PLANE, "FB_ID", buf.fb_id);
    add_p(plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_ID", crtc_id);
    add_p(plane_id, DRM_MODE_OBJECT_PLANE, "SRC_X", 0);
    add_p(plane_id, DRM_MODE_OBJECT_PLANE, "SRC_Y", 0);
    add_p(plane_id, DRM_MODE_OBJECT_PLANE, "SRC_W", (uint64_t)mode.hdisplay << 16);
    add_p(plane_id, DRM_MODE_OBJECT_PLANE, "SRC_H", (uint64_t)mode.vdisplay << 16);
    add_p(plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_X", 0);
    add_p(plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_Y", 0);
    add_p(plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_W", mode.hdisplay);
    add_p(plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_H", mode.vdisplay);
    add_p(plane_id, DRM_MODE_OBJECT_PLANE, "ZPOS", 2);

    uint32_t mode_blob_id;
    drmModeCreatePropertyBlob(fd, &mode, sizeof(mode), &mode_blob_id);
    add_p(crtc_id, DRM_MODE_OBJECT_CRTC, "MODE_ID", mode_blob_id);
    add_p(crtc_id, DRM_MODE_OBJECT_CRTC, "ACTIVE", 1);
    add_p(conn->connector_id, DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID", crtc_id);

    int ret = drmModeAtomicCommit(fd, req, DRM_MODE_ATOMIC_ALLOW_MODESET, NULL);
    if (ret != 0) {
        std::cerr << "CRITICAL FAIL! Error Code: " << ret << " (" << strerror(errno) << ")" << std::endl;
    } else {
        std::cout << "Atomic success! Displaying..." << std::endl;
        sleep(5);
    }

    drmModeAtomicFree(req);
    close(fd);
    return 0;
}

// How to build:
// g++ drm_cairo.cpp -o drm_cairo $(pkg-config --cflags --libs libdrm cairo)