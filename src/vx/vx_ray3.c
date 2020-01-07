#include "vx_ray3.h"
#include <math.h>

void vx_ray3_intersect_xy(vx_ray3_t * ray, double zheight, double * vec3_out)
{
    double dist = (ray->source[2] - zheight)/ray->dir[2];

    vec3_out[0] = ray->source[0] - ray->dir[0]*dist;
    vec3_out[1] = ray->source[1] - ray->dir[1]*dist;
    vec3_out[2] = zheight;
}

void vx_ray3_closest_point_xyz(vx_ray3_t * ray, double p[3], double * vec3_out) {

    double n[3];
    double mag_d = sqrt(ray->dir[0]*ray->dir[0] +
                        ray->dir[1]*ray->dir[1] +
                        ray->dir[2]*ray->dir[2]);

    n[0] = ray->dir[0]/mag_d;
    n[1] = ray->dir[1]/mag_d;
    n[2] = ray->dir[2]/mag_d;

    double s_p[3];
    s_p[0]  = ray->source[0] - p[0];
    s_p[1]  = ray->source[1] - p[1];
    s_p[2]  = ray->source[2] - p[2];

    double s_p_dot_n = s_p[0]*n[0] + s_p[1]*n[1] + s_p[2]*n[2];
    vec3_out[0] = ray->source[0] - n[0] * s_p_dot_n;
    vec3_out[1] = ray->source[1] - n[1] * s_p_dot_n;
    vec3_out[2] = ray->source[2] - n[2] * s_p_dot_n;
}
