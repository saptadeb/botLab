#ifndef VX_RAY3_H
#define VX_RAY3_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    double source[3];
    double dir[3];
} vx_ray3_t;

void vx_ray3_intersect_xy(vx_ray3_t * ray, double zheight, double * vec3_out);
void vx_ray3_closest_point_xyz(vx_ray3_t * ray, double p[3], double * vec3_out);

#ifdef __cplusplus
}
#endif

#endif
