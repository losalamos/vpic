// Note: This is similar to advance_e_pipeline

#include <field.h>

// See note and advance_e
#undef V4_ACCELERATION

#ifndef V4_ACCELERATION
#define COMPUTE_CURL_B_PIPELINE (pipeline_func_t)compute_curl_b_pipeline
#else
#define COMPUTE_CURL_B_PIPELINE (pipeline_func_t)compute_curl_b_pipeline_v4
#endif

#define f(x,y,z) f[INDEX_FORTRAN_3(x,y,z,0,nx+1,0,ny+1,0,nz+1)]

#define UPDATE_TCAX()						          \
  f0->tcax = py*(f0->cbz*m[f0->fmatz].rmuz-fy->cbz*m[fy->fmatz].rmuz) -   \
             pz*(f0->cby*m[f0->fmaty].rmuy-fz->cby*m[fz->fmaty].rmuy)

#define UPDATE_TCAY()						          \
  f0->tcay = pz*(f0->cbx*m[f0->fmatx].rmux-fz->cbx*m[fz->fmatx].rmux) -   \
             px*(f0->cbz*m[f0->fmatz].rmuz-fx->cbz*m[fx->fmatz].rmuz)

#define UPDATE_TCAZ()						          \
  f0->tcaz = px*(f0->cby*m[f0->fmaty].rmuy-fx->cby*m[fx->fmaty].rmuy) -   \
             py*(f0->cbx*m[f0->fmatx].rmux-fy->cbx*m[fy->fmatx].rmux)

typedef struct compute_curl_b_pipeline_args {
  field_t                      * ALIGNED f;
  const material_coefficient_t * ALIGNED m;
  const grid_t                 *         g;
} compute_curl_b_pipeline_args_t;
         
static void
compute_curl_b_pipeline( compute_curl_b_pipeline_args_t * args,
                         int pipeline_rank,
                         int n_pipeline ) {
  field_t                      * ALIGNED f = args->f;
  const material_coefficient_t * ALIGNED m = args->m;
  const grid_t                 *         g = args->g;

  field_t * ALIGNED f0, * ALIGNED fx, * ALIGNED fy, * ALIGNED fz;
  int x, y, z, n_voxel;

  const int nx = g->nx;
  const int ny = g->ny;
  const int nz = g->nz;

  const float px = (nx>1) ? g->cvac*g->dt/g->dx : 0;
  const float py = (ny>1) ? g->cvac*g->dt/g->dy : 0;
  const float pz = (nz>1) ? g->cvac*g->dt/g->dz : 0;

  /* Process the voxels assigned to this pipeline */
  
  n_voxel = distribute_voxels( 2,nx, 2,ny, 2,nz,
                               pipeline_rank, n_pipeline,
                               &x, &y, &z );

# define LOAD_STENCIL() \
  f0 = &f(x,  y,  z  ); \
  fx = &f(x-1,y,  z  ); \
  fy = &f(x,  y-1,z  ); \
  fz = &f(x,  y,  z-1)
  
  LOAD_STENCIL();

  for( ; n_voxel; n_voxel-- ) {
    UPDATE_TCAX();
    UPDATE_TCAY();
    UPDATE_TCAZ(); 
    f0++; fx++;	fy++; fz++;
    
    x++;
    if( x>nx ) {
      x=2, y++;
      if( y>ny ) y=2, z++;
      LOAD_STENCIL();
    }
  }

# undef LOAD_STENCIL

}

#ifdef V4_ACCELERATION

using namespace v4;

static void
compute_curl_b_pipeline_v4( compute_curl_b_pipeline_args_t * args,
                            int pipeline_rank,
                            int n_pipeline ) {
  field_t                      * ALIGNED f = args->f;
  const material_coefficient_t * ALIGNED m = args->m;
  const grid_t                 *         g = args->g;

  field_t * ALIGNED f0, * ALIGNED fx, * ALIGNED fy, * ALIGNED fz;
  int x, y, z, n_voxel;

  const int nx = g->nx;
  const int ny = g->ny;
  const int nz = g->nz;

  const float px = (nx>1) ? g->cvac*g->dt/g->dx : 0;
  const float py = (ny>1) ? g->cvac*g->dt/g->dy : 0;
  const float pz = (nz>1) ? g->cvac*g->dt/g->dz : 0;

  const v4float vpx(px);
  const v4float vpy(py);
  const v4float vpz(pz);

  v4float f0_cbx,  f0_cby,  f0_cbz;
  v4float f0_tcax, f0_tcay, f0_tcaz;
  v4float          fx_cby,  fx_cbz;
  v4float fy_cbx,           fy_cbz;
  v4float fz_cbx,  fz_cby;
  v4float m_f0_rmux, m_f0_rmuy, m_f0_rmuz;
  v4float            m_fx_rmuy, m_fx_rmuz;
  v4float m_fy_rmux,            m_fy_rmuz;
  v4float m_fz_rmux, m_fz_rmuy;

  v4float f0_cbx_rmux, f0_cby_rmuy, f0_cbz_rmuz;

  field_t * ALIGNED f00, * ALIGNED f01, * ALIGNED f02, * ALIGNED f03; // Voxel quad
  field_t * ALIGNED fx0, * ALIGNED fx1, * ALIGNED fx2, * ALIGNED fx3; // Voxel quad +x neighbors
  field_t * ALIGNED fy0, * ALIGNED fy1, * ALIGNED fy2, * ALIGNED fy3; // Voxel quad +y neighbors
  field_t * ALIGNED fz0, * ALIGNED fz1, * ALIGNED fz2, * ALIGNED fz3; // Voxel quad +z neighbors

  // Process the voxels assigned to this pipeline 
  
  n_voxel = distribute_voxels( 2,nx, 2,ny, 2,nz,
                               pipeline_rank, n_pipeline,
                               &x, &y, &z );

  // Process the bulk of the voxels 4 at a time
                               
# define LOAD_STENCIL() \
  f0 = &f(x,  y,  z  ); \
  fx = &f(x-1,y,  z  ); \
  fy = &f(x,  y-1,z  ); \
  fz = &f(x,  y,  z-1)

# define NEXT_STENCIL(n) \
  f0##n = f0++;          \
  fx##n = fx++;          \
  fy##n = fy++;          \
  fz##n = fz++;          \
  x++;                   \
  if( x>nx ) {           \
    x=2, y++;            \
    if( y>ny ) y=2, z++; \
    LOAD_STENCIL();      \
  }

  LOAD_STENCIL();

  for( ; n_voxel>3; n_voxel-=4 ) {
    NEXT_STENCIL(0); NEXT_STENCIL(1); NEXT_STENCIL(2); NEXT_STENCIL(3);

    load_4x1_tr( &f00->cbx,   &f01->cbx,   &f02->cbx,   &f03->cbx,                                 f0_cbx );
    load_4x2_tr( &f00->cby,   &f01->cby,   &f02->cby,   &f03->cby,   f0_cby,   f0_cbz                     );

    load_4x2_tr( &fx0->cby,   &fx1->cby,   &fx2->cby,   &fx3->cby,   fx_cby,   fx_cbz                     );

    load_4x1_tr( &fy0->cbx,   &fy1->cbx,   &fy2->cbx,   &fy3->cbx,                                 fy_cbx );
    load_4x1_tr( &fy0->cbz,   &fy1->cbz,   &fy2->cbz,   &fy3->cbz,             fy_cbz                     );

    load_4x1_tr( &fz0->cbx,   &fz1->cbx,   &fz2->cbx,   &fz3->cbx,                                 fy_cbx );
    load_4x1_tr( &fz0->cby,   &fz1->cby,   &fz2->cby,   &fz3->cby,   fz_cby                               );

#   define LOAD_RMU( V, D ) load_4x1_tr( &m[f##V##0->fmat##D].rmu##D, \
                                         &m[f##V##1->fmat##D].rmu##D, \
                                         &m[f##V##2->fmat##D].rmu##D, \
                                         &m[f##V##3->fmat##D].rmu##D, \
                                         m_f##V##_rmu##D )

    LOAD_RMU(0,x); LOAD_RMU(0,y); LOAD_RMU(0,z);
    /**/           LOAD_RMU(x,y); LOAD_RMU(x,z);
    LOAD_RMU(y,x);                LOAD_RMU(y,z);
    LOAD_RMU(z,x); LOAD_RMU(z,y);

#   undef LOAD_RMU
    
    f0_cbx_rmux = f0_cbx * m_f0_rmux;
    f0_cby_rmuy = f0_cby * m_f0_rmuy;
    f0_cbz_rmuz = f0_cbz * m_f0_rmuz;

    f0_tcax = fms( vpy,fnms( fy_cbz,m_fy_rmuz, f0_cbz_rmuz ),
                   vpz*fnms( fz_cby,m_fz_rmuy, f0_cby_rmuy ) );

    f0_tcay = fms( vpz,fnms( fz_cbx,m_fz_rmux, f0_cbx_rmux ),
                   vpx*fnms( fx_cbz,m_fx_rmuz, f0_cbz_rmuz ) );

    f0_tcaz = fms( vpx,fnms( fx_cby,m_fx_rmuy, f0_cby_rmuy ),
                   vpy*fnms( fy_cbx,m_fy_rmux, f0_cbx_rmux ) );

    store_4x2_tr(               f0_tcax, f0_tcay, &f00->tcax,  &f01->tcax,  &f02->tcax,  &f03->tcax );
    store_4x1_tr( f0_tcaz,                        &f00->tcaz,  &f01->tcaz,  &f02->tcaz,  &f03->tcaz );
  }

# undef NEXT_STENCIL
# undef LOAD_STENCIL

}

#endif

void
compute_curl_b( field_t * ALIGNED f,
                const material_coefficient_t * ALIGNED m,
                const grid_t * g ) {
  compute_curl_b_pipeline_args_t args[1];
  
  float px, py, pz;
  field_t *f0, *fx, *fy, *fz;
  int x, y, z, nx, ny, nz;

  if( f==NULL ) ERROR(("Bad field"));
  if( m==NULL ) ERROR(("Bad material coefficients"));
  if( g==NULL ) ERROR(("Bad grid"));

  nx = g->nx;
  ny = g->ny;
  nz = g->nz;
  px = (nx>1) ? g->cvac*g->dt/g->dx : 0;
  py = (ny>1) ? g->cvac*g->dt/g->dy : 0;
  pz = (nz>1) ? g->cvac*g->dt/g->dz : 0;

  /***************************************************************************
   * Begin tangential B ghost setup
   ***************************************************************************/
  
  begin_remote_ghost_tang_b( f, g );
  local_ghost_tang_b( f, g );

  /***************************************************************************
   * Update interior fields
   * Note: ex all (1:nx,  1:ny+1,1,nz+1) interior (1:nx,2:ny,2:nz)
   * Note: ey all (1:nx+1,1:ny,  1:nz+1) interior (2:nx,1:ny,2:nz)
   * Note: ez all (1:nx+1,1:ny+1,1:nz  ) interior (1:nx,1:ny,2:nz)
   ***************************************************************************/

  /* Do bulk of the interior in a single pass in the pipelines. (The
     host handles stragglers in the interior.) */
  /* FIXME: CHECK IF IT IS SAFE TO DISPATCH THE PIPELINES EVEN EARLIER
     AND COMPLETE THEM EVEN LATER.  I DON'T THINK IT IS SAFE TO
     DISPATCH THEM EARLIER UNDER ABSORBING BOUNDARY CONDITIONS BUT I
     AM NOT SURE.  I THINK IT IS PROBABLY SAFE TO FINISH THEM
     LATER. */

# if 0 /* Original non-pipelined version */
  for( z=2; z<=nz; z++ ) {
    for( y=2; y<=ny; y++ ) {
      f0 = &f(2,y,  z);
      fx = &f(1,y,  z);
      fy = &f(2,y-1,z);
      fz = &f(2,y,  z-1);
      for( x=2; x<=nx; x++ ) {
	UPDATE_TCAX();
	UPDATE_TCAY();
	UPDATE_TCAZ();
	f0++; fx++; fy++; fz++;
      }
    }
  }
# endif
     
  args->f = f;
  args->m = m;
  args->g = g;

  PSTYLE.dispatch( COMPUTE_CURL_B_PIPELINE, args, 0 );
  compute_curl_b_pipeline( args, PSTYLE.n_pipeline, PSTYLE.n_pipeline );
  
  /* Do left over interior ex */
  for( z=2; z<=nz; z++ ) {
    for( y=2; y<=ny; y++ ) {
      f0 = &f(1,y,  z);
      fy = &f(1,y-1,z);
      fz = &f(1,y,  z-1);
      UPDATE_TCAX();
    }
  }

  /* Do left over interior ey */
  for( z=2; z<=nz; z++ ) {
    f0 = &f(2,1,z);
    fx = &f(1,1,z);
    fz = &f(2,1,z-1);
    for( x=2; x<=nx; x++ ) {
      UPDATE_TCAY();
      f0++;
      fx++;
      fz++;
    }
  }

  /* Do left over interior ez */
  for( y=2; y<=ny; y++ ) {
    f0 = &f(2,y,  1);
    fx = &f(1,y,  1);
    fy = &f(2,y-1,1);
    for( x=2; x<=nx; x++ ) {
      UPDATE_TCAZ();
      f0++;
      fx++;
      fy++;
    }
  }

  PSTYLE.wait();
  
  /***************************************************************************
   * Finish tangential B ghost setup
   ***************************************************************************/

  end_remote_ghost_tang_b( f, g );

  /***************************************************************************
   * Update exterior fields
   ***************************************************************************/

  /* Do exterior ex */
  for( y=1; y<=ny+1; y++ ) {
    f0 = &f(1,y,  1);
    fy = &f(1,y-1,1);
    fz = &f(1,y,  0);
    for( x=1; x<=nx; x++ ) {
      UPDATE_TCAX();
      f0++;
      fy++;
      fz++;
    }
  }
  for( y=1; y<=ny+1; y++ ) {
    f0 = &f(1,y,  nz+1);
    fy = &f(1,y-1,nz+1);
    fz = &f(1,y,  nz);
    for( x=1; x<=nx; x++ ) {
      UPDATE_TCAX();
      f0++;
      fy++;
      fz++;
    }
  }
  for( z=2; z<=nz; z++ ) {
    f0 = &f(1,1,z);
    fy = &f(1,0,z);
    fz = &f(1,1,z-1);
    for( x=1; x<=nx; x++ ) {
      UPDATE_TCAX();
      f0++;
      fy++;
      fz++;
    }
  }
  for( z=2; z<=nz; z++ ) {
    f0 = &f(1,ny+1,z);
    fy = &f(1,ny,  z);
    fz = &f(1,ny+1,z-1);
    for( x=1; x<=nx; x++ ) {
      UPDATE_TCAX();
      f0++;
      fy++;
      fz++;
    }
  }

  /* Do exterior ey */
  for( z=1; z<=nz+1; z++ ) {
    for( y=1; y<=ny; y++ ) {
      f0 = &f(1,y,z);
      fx = &f(0,y,z);
      fz = &f(1,y,z-1);
      UPDATE_TCAY();
    }
  }
  for( z=1; z<=nz+1; z++ ) {
    for( y=1; y<=ny; y++ ) {
      f0 = &f(nx+1,y,z);
      fx = &f(nx,  y,z);
      fz = &f(nx+1,y,z-1);
      UPDATE_TCAY();
    }
  }
  for( y=1; y<=ny; y++ ) {
    f0 = &f(2,y,1);
    fx = &f(1,y,1);
    fz = &f(2,y,0);
    for( x=2; x<=nx; x++ ) {
      UPDATE_TCAY();
      f0++;
      fx++;
      fz++;
    }
  }
  for( y=1; y<=ny; y++ ) {
    f0 = &f(2,y,nz+1);
    fx = &f(1,y,nz+1);
    fz = &f(2,y,nz  );
    for( x=2; x<=nx; x++ ) {
      UPDATE_TCAY();
      f0++;
      fx++;
      fz++;
    }
  }

  /* Do exterior ez */
  for( z=1; z<=nz; z++ ) {
    f0 = &f(1,1,z);
    fx = &f(0,1,z);
    fy = &f(1,0,z);
    for( x=1; x<=nx+1; x++ ) {
      UPDATE_TCAZ();
      f0++;
      fx++;
      fy++;
    }
  }
  for( z=1; z<=nz; z++ ) {
    f0 = &f(1,ny+1,z);
    fx = &f(0,ny+1,z);
    fy = &f(1,ny,  z);
    for( x=1; x<=nx+1; x++ ) {
      UPDATE_TCAZ();
      f0++;
      fx++;
      fy++;
    }
  }
  for( z=1; z<=nz; z++ ) {
    for( y=2; y<=ny; y++ ) {
      f0 = &f(1,y,  z);
      fx = &f(0,y,  z);
      fy = &f(1,y-1,z);
      UPDATE_TCAZ();
    }
  }
  for( z=1; z<=nz; z++ ) {
    for( y=2; y<=ny; y++ ) {
      f0 = &f(nx+1,y,  z);
      fx = &f(nx,  y,  z);
      fy = &f(nx+1,y-1,z);
      UPDATE_TCAZ();
    }
  }

  local_adjust_tang_e( f, g );
}