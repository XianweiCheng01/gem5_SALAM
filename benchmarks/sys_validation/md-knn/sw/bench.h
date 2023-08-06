#include "../defines.h"
#include "data.h"

#define EPSILON 1.0e-2

volatile int stage;

typedef struct {
    TYPE * force_x;
    TYPE * force_y;
    TYPE * force_z;
    TYPE * position_x;
    TYPE * position_y;
    TYPE * position_z;
    int32_t * NL;
    TYPE * check_x;
    TYPE * check_y;
    TYPE * check_z;
} md_struct;

void genData(md_struct * mds) {
    int i;
    for(i = 0; i < nAtoms; i++) {
        mds->check_x[i]     = c_x[i];
        mds->check_y[i]     = c_y[i];
        mds->check_z[i]     = c_z[i];
        mds->position_x[i]  = p_x[i];
        mds->position_y[i]  = p_y[i];
        mds->position_z[i]  = p_z[i];
    }
    for(i = 0; i < nAtoms*maxNeighbors; i++) {
        mds->NL[i] = n_l[i];
    }
}

void md_kernel_cpu(TYPE* force_x_base, TYPE* force_y_base, TYPE* force_z_base, TYPE* position_x_base, TYPE* position_y_base, TYPE* position_z_base, int32_t* nl_base) {

    //uint8_t * force_x_base    = (uint8_t *)FORCEXADDR;
    //uint8_t * force_y_base    = (uint8_t *)FORCEYADDR;
    //uint8_t * force_z_base    = (uint8_t *)FORCEZADDR;
    //uint8_t * position_x_base = (uint8_t *)POSITIONXADDR;
    //uint8_t * position_y_base = (uint8_t *)POSITIONYADDR;
    //uint8_t * position_z_base = (uint8_t *)POSITIONZADDR;
    //uint8_t * nl_base         = (uint8_t *)NLADDR;
    TYPE    * force_x         = (TYPE    *)force_x_base;
    TYPE    * force_y         = (TYPE    *)force_y_base;
    TYPE    * force_z         = (TYPE    *)force_z_base;
    TYPE    * position_x      = (TYPE    *)position_x_base;
    TYPE    * position_y      = (TYPE    *)position_y_base;
    TYPE    * position_z      = (TYPE    *)position_z_base;
    int32_t * NL              = (int32_t *)nl_base;

    TYPE delx, dely, delz, r2inv;
    TYPE r6inv, potential, force, j_x, j_y, j_z;
    TYPE i_x, i_y, i_z, fx, fy, fz;
    int32_t i, j, jidx;

    loop_i :
    for (i = 0; i < nAtoms; i++) {
        i_x = position_x[i];
        i_y = position_y[i];
        i_z = position_z[i];
        fx = 0;
        fy = 0;
        fz = 0;

        loop_j :
        for( j = 0; j < maxNeighbors; j++) {
            // Get neighbor
            jidx = NL[i*maxNeighbors + j];
            // Look up x,y,z positions
            j_x = position_x[jidx];
            j_y = position_y[jidx];
            j_z = position_z[jidx];
            // Calc distance
            delx = i_x - j_x;
            dely = i_y - j_y;
            delz = i_z - j_z;
            r2inv = 1.0/( delx*delx + dely*dely + delz*delz );
            // Assume no cutoff and aways account for all nodes in area
            r6inv = r2inv * r2inv * r2inv;
            potential = r6inv*(lj1*r6inv - lj2);
            // Sum changes in force
            force = r2inv*potential;
            fx += delx * force;
            fy += dely * force;
            fz += delz * force;
        }
        //Update forces after all neighbors accounted for.
        force_x[i] = fx;
        force_y[i] = fy;
        force_z[i] = fz;
    }
}
