#include "gpu_accel.h"

#import <Metal/Metal.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    id<MTLDevice> device;
    id<MTLBuffer> buffer;
    id<MTLCommandQueue> queue;
    id<MTLComputePipelineState> face_count_pipeline;
    id<MTLComputePipelineState> face_emit_pipeline;
    id<MTLComputePipelineState> vertex_emit_pipeline;
    id<MTLComputePipelineState> mc_emit_pipeline;
    id<MTLComputePipelineState> path_preview_pipeline;
    id<MTLBuffer> mesh_input;
    id<MTLBuffer> mesh_count;
    id<MTLBuffer> mesh_faces;
    id<MTLBuffer> mesh_vertices;
    id<MTLBuffer> mc_tri_table;
    id<MTLBuffer> path_hash;
    id<MTLBuffer> path_accum;
    id<MTLBuffer> path_output;
    size_t path_hash_size;
    size_t path_pixel_capacity;
    size_t size;
} gpu_metal_mirror_t;

void gpu_accel_metal_mirror_destroy(void *ptr);

static NSString *const BLOCK_MESH_SOURCE =
@"#include <metal_stdlib>\n"
"using namespace metal;\n"
"kernel void count_block_faces(device const uchar4 *voxels [[buffer(0)]],\n"
"                              device atomic_uint *face_count [[buffer(1)]],\n"
"                              uint tid [[thread_position_in_grid]]) {\n"
"    if (tid >= 4096) return;\n"
"    uint x = tid & 15, y = (tid >> 4) & 15, z = tid >> 8;\n"
"    uint sx = 18, sy = 18 * 18;\n"
"    uint p = (x + 1) + (y + 1) * sx + (z + 1) * sy;\n"
"    if (voxels[p].a < 127) return;\n"
"    uint visible = 0;\n"
"    visible += voxels[p - sx].a < 127;\n"
"    visible += voxels[p + sx].a < 127;\n"
"    visible += voxels[p - sy].a < 127;\n"
"    visible += voxels[p + sy].a < 127;\n"
"    visible += voxels[p + 1].a < 127;\n"
"    visible += voxels[p - 1].a < 127;\n"
"    atomic_fetch_add_explicit(face_count, visible, memory_order_relaxed);\n"
"}\n";

static NSString *const BLOCK_EMIT_SOURCE =
@"#include <metal_stdlib>\n"
"using namespace metal;\n"
"kernel void emit_block_faces(device const uchar4 *voxels [[buffer(0)]],\n"
"                             device atomic_uint *face_count [[buffer(1)]],\n"
"                             device uint *faces [[buffer(2)]],\n"
"                             constant uint &capacity [[buffer(3)]],\n"
"                             uint tid [[thread_position_in_grid]]) {\n"
"    if (tid >= 4096) return;\n"
"    uint x = tid & 15, y = (tid >> 4) & 15, z = tid >> 8;\n"
"    uint sx = 18, sy = 18 * 18;\n"
"    uint p = (x + 1) + (y + 1) * sx + (z + 1) * sy;\n"
"    if (voxels[p].a < 127) return;\n"
"    int offsets[6] = {-int(sx), int(sx), -int(sy), int(sy), 1, -1};\n"
"    for (uint f = 0; f < 6; f++) {\n"
"        if (voxels[p + offsets[f]].a >= 127) continue;\n"
"        uint dst = atomic_fetch_add_explicit(face_count, 1u,\n"
"                                              memory_order_relaxed);\n"
"        if (dst < capacity)\n"
"            faces[dst] = (x << 12) | (y << 8) | (z << 4) | f;\n"
"    }\n"
"}\n";

static NSString *const BLOCK_VERTEX_SOURCE =
@"#include <metal_stdlib>\n"
"using namespace metal;\n"
"struct Vertex { uchar4 pos; char4 normal; char4 tangent; char4 gradient;\n"
" uchar4 color; ushort pos_data; ushort pad0; uchar2 uv; uchar2 pad1;\n"
" uchar2 occlusion_uv; uchar2 pad2; uchar2 bump_uv; uchar2 pad3; };\n"
"constant int3 normals[6] = {int3(0,-1,0),int3(0,1,0),int3(0,0,-1),\n"
" int3(0,0,1),int3(1,0,0),int3(-1,0,0)};\n"
"constant int3 tangents[6] = {int3(1,0,0),int3(-1,0,0),int3(0,1,0),\n"
" int3(0,1,0),int3(0,1,0),int3(0,0,1)};\n"
"constant uchar faceVerts[24] = {0,1,2,3,5,4,7,6,0,4,5,1,\n"
" 2,6,7,3,1,5,6,2,0,3,7,4};\n"
"constant uchar3 vertPos[8] = {uchar3(0,0,0),uchar3(1,0,0),\n"
" uchar3(1,0,1),uchar3(0,0,1),uchar3(0,1,0),uchar3(1,1,0),\n"
" uchar3(1,1,1),uchar3(0,1,1)};\n"
"constant uchar2 vertUV[4] = {uchar2(0,0),uchar2(1,0),\n"
" uchar2(1,1),uchar2(0,1)};\n"
"constant uchar faceNeighbors[24] = {4,3,5,2,5,3,4,2,1,4,0,5,\n"
" 1,5,0,4,1,3,0,2,3,1,2,0};\n"
"constant uint edgeMasks[24] = {0x2,0x800,0x80000,0x200,0x80,0x8000,\n"
" 0x2000000,0x20000,0x8,0x80,0x20,0x2,0x800000,0x2000000,\n"
" 0x200000,0x80000,0x20,0x20000,0x800000,0x800,0x200,0x200000,\n"
" 0x8000,0x8};\n"
"constant uint cornerMasks[24] = {0x203,0x806,0x180800,0xc0200,\n"
" 0x20180,0x80c0,0x3008000,0x6020000,0xb,0xc8,0x1a0,0x26,\n"
" 0x980000,0x6800000,0x3200000,0x2c0000,0x824,0x20120,\n"
" 0x4820000,0x900800,0x209,0x240200,0x1208000,0x8048};\n"
"inline uint bitIndex(int3 p) { return uint((p.x+1)+(p.y+1)*3+(p.z+1)*9); }\n"
"kernel void emit_block_vertices(device const uchar4 *vox [[buffer(0)]],\n"
" device atomic_uint *count [[buffer(1)]], device Vertex *out [[buffer(2)]],\n"
" constant uint &capacity [[buffer(3)]], uint tid [[thread_position_in_grid]]) {\n"
" if (tid>=4096) return; uint x=tid&15,y=(tid>>4)&15,z=tid>>8;\n"
" uint p=(x+1)+(y+1)*18+(z+1)*324; uchar4 color=vox[p];\n"
" if(color.a<127) return; uint mask=0; uchar alpha[27]; uint ni=0;\n"
" for(int dz=-1;dz<=1;dz++) for(int dy=-1;dy<=1;dy++)\n"
" for(int dx=-1;dx<=1;dx++){uint q=p+dx+dy*18+dz*324;\n"
"  alpha[ni]=vox[q].a; if(alpha[ni]>=127) mask|=1u<<ni; ni++;}\n"
" for(uint f=0;f<6;f++){int3 n=normals[f];\n"
"  if(mask&(1u<<bitIndex(n))) continue;\n"
"  uint dst=atomic_fetch_add_explicit(count,1u,memory_order_relaxed);\n"
"  if(dst>=capacity) continue; int sx=0,sy=0,sz=0; ni=0;\n"
"  for(int dz=-1;dz<=1;dz++) for(int dy=-1;dy<=1;dy++)\n"
"  for(int dx=-1;dx<=1;dx++){if(mask&(1u<<ni)){sx-=int(alpha[ni])*dx;\n"
"   sy-=int(alpha[ni])*dy;sz-=int(alpha[ni])*dz;}ni++;}\n"
"  int3 grad=n; int gm=max(abs(sx),max(abs(sy),abs(sz)));\n"
"  if(gm) grad=int3(sx,sy,sz)*127/gm; uint shadow=0,border=0;\n"
"  for(uint e=0;e<4;e++){uint k=f*4+e;\n"
"   if(mask&cornerMasks[k]) shadow|=1u<<e;\n"
"   if(mask&edgeMasks[k]) shadow|=0x10u<<e;\n"
"   int3 t=normals[faceNeighbors[k]];\n"
"   if(mask&(1u<<bitIndex(n+t))) border|=2u<<(2*e);\n"
"   else if(!(mask&(1u<<bitIndex(t)))) border|=1u<<(2*e);}\n"
"  for(uint v=0;v<4;v++){Vertex r={}; uchar3 vp=vertPos[faceVerts[f*4+v]];\n"
"   r.pos=uchar4(uchar(x)+vp.x,uchar(y)+vp.y,uchar(z)+vp.z,0);\n"
"   r.normal=char4(char(n.x),char(n.y),char(n.z),0);\n"
"   int3 t=tangents[f];r.tangent=char4(char(t.x),char(t.y),char(t.z),0);\n"
"   r.gradient=char4(char(grad.x),char(grad.y),char(grad.z),0);\n"
"   r.color=uchar4(color.rgb,255);r.pos_data=ushort((x<<12)|(y<<8)|(z<<4)|f);\n"
"   r.uv=vertUV[v]*255;r.occlusion_uv=uchar2((shadow%16)*8+vertUV[v].x*7,\n"
"    (shadow/16)*8+vertUV[v].y*7);r.bump_uv=uchar2((border%16)*16,\n"
"    (border/16)*16);out[dst*4+v]=r;}\n"
" }\n"
"}\n";

static NSString *const MC_VERTEX_SOURCE =
@"#include <metal_stdlib>\n"
"using namespace metal;\n"
"struct Vertex { uchar4 pos; char4 normal; char4 tangent; char4 gradient;\n"
" uchar4 color; ushort pos_data; ushort pad0; uchar2 uv; uchar2 pad1;\n"
" uchar2 occlusion_uv; uchar2 pad2; uchar2 bump_uv; uchar2 pad3; };\n"
"constant uchar3 vp[8]={uchar3(0,0,0),uchar3(1,0,0),uchar3(1,0,1),\n"
" uchar3(0,0,1),uchar3(0,1,0),uchar3(1,1,0),uchar3(1,1,1),uchar3(0,1,1)};\n"
"constant uchar2 ev[12]={uchar2(0,1),uchar2(1,2),uchar2(2,3),uchar2(3,0),\n"
" uchar2(4,5),uchar2(5,6),uchar2(6,7),uchar2(7,4),uchar2(0,4),\n"
" uchar2(1,5),uchar2(2,6),uchar2(3,7)};\n"
"kernel void emit_mc_vertices(device const uchar4 *vox [[buffer(0)]],\n"
" device atomic_uint *count [[buffer(1)]],device Vertex *out [[buffer(2)]],\n"
" constant uint &capacity [[buffer(3)]],device const char *table [[buffer(4)]],\n"
" uint tid [[thread_position_in_grid]]){\n"
" if(tid>=4096)return;uint x=tid&15,y=(tid>>4)&15,z=tid>>8;\n"
" uint base=(x+1)+(y+1)*18+(z+1)*324;uchar4 c[8];uint ci=0;\n"
" for(uint i=0;i<8;i++){uint q=base+vp[i].x+vp[i].y*18+vp[i].z*324;\n"
"  c[i]=vox[q];if(c[i].a>=127)ci|=1u<<i;}\n"
" device const char *row=table+ci*16;\n"
" for(uint ti=0;ti<15&&row[ti]>=0;ti+=3){uint dst=atomic_fetch_add_explicit(\n"
"  count,1u,memory_order_relaxed);if(dst>=capacity)continue;float3 p[3];\n"
"  uchar4 colors[3];for(uint j=0;j<3;j++){uint e=uint(row[ti+j]);uint a=ev[e].x,b=ev[e].y;\n"
"   float f0=float(c[a].a)/255.0,f1=float(c[b].a)/255.0;\n"
"   float mu=(f0-0.5)/(f0-f1);p[j]=(float3(vp[a])*(1.0-mu)+float3(vp[b])*mu)*8.0;\n"
"   colors[j]=c[a].a>c[b].a?c[a]:c[b];}\n"
"  float3 n=normalize(cross(p[1]-p[0],p[2]-p[0]));\n"
"  for(uint j=0;j<3;j++){Vertex r={};float3 fp=p[j]+float3(x,y,z)*8.0+4.5;\n"
"   r.pos=uchar4(uchar3(fp),0);r.normal=char4(char3(n*64.0),0);\n"
"   r.color=uchar4(colors[j].rgb,255);out[dst*3+j]=r;}\n"
" }\n"
"}\n";

static NSString *const PATH_PREVIEW_SOURCE =
@"#include <metal_stdlib>\n"
"using namespace metal;\n"
"inline uint hash_pos(int3 p){return (uint(p.x)*73856093u)^\n"
" (uint(p.y)*19349663u)^(uint(p.z)*83492791u);}\n"
"inline int tile_coord(int v){return v>=0?(v/16)*16:((v-15)/16)*16;}\n"
"inline uchar4 voxel_at(device const uchar4 *vox,device const int4 *table,\n"
" uint cap,int3 p){int3 o=int3(tile_coord(p.x),tile_coord(p.y),tile_coord(p.z));\n"
" uint i=hash_pos(o)&(cap-1);for(uint probe=0;probe<cap;probe++){\n"
"  int4 e=table[i];if(e.w<0)return uchar4(0);if(all(e.xyz==o)){\n"
"   int3 q=p-o;uint n=uint(q.x+q.y*16+q.z*256);\n"
"   return vox[uint(e.w)*4096u+n];}i=(i+1)&(cap-1);}return uchar4(0);}\n"
"inline bool box_hit(float3 ro,float3 rd,float3 b0,float3 b1,\n"
" thread float &tn,thread float &tf){float3 inv=1.0/rd;\n"
" float3 a=(b0-ro)*inv,b=(b1-ro)*inv;float3 lo=min(a,b),hi=max(a,b);\n"
" tn=max(max(lo.x,lo.y),lo.z);tf=min(min(hi.x,hi.y),hi.z);\n"
" return tf>=max(tn,0.0);}\n"
"inline bool trace_vox(device const uchar4 *vox,device const int4 *table,\n"
" uint cap,int3 b0,int3 b1,float3 ro,float3 rd,uint maxSteps,\n"
" thread uchar4 &hit,thread float3 &normal,thread int3 &hitCell){float tn,tf;\n"
" if(!box_hit(ro,rd,float3(b0),float3(b1),tn,tf))return false;\n"
" float t=max(tn,0.0)+0.0001;float3 p=ro+rd*t;int3 cell=int3(floor(p));\n"
" int3 step=select(int3(-1),int3(1),rd>=0.0);float3 delta=abs(1.0/rd);\n"
" float3 next=float3(cell)+select(float3(0.0),float3(1.0),step>0);\n"
" float3 side=(next-ro)/rd;normal=float3(0.0);\n"
" for(uint n=0;n<maxSteps&&t<=tf;n++){hit=voxel_at(vox,table,cap,cell);\n"
"  if(hit.a>=127){hitCell=cell;return true;}if(side.x<=side.y&&side.x<=side.z){\n"
"   t=side.x;side.x+=delta.x;cell.x+=step.x;normal=float3(-step.x,0,0);\n"
"  }else if(side.y<=side.z){t=side.y;side.y+=delta.y;cell.y+=step.y;\n"
"   normal=float3(0,-step.y,0);}else{t=side.z;side.z+=delta.z;\n"
"   cell.z+=step.z;normal=float3(0,0,-step.z);}}\n"
" return false;}\n"
"inline float random01(uint v){v^=v>>16;v*=0x7feb352du;v^=v>>15;\n"
" v*=0x846ca68bu;v^=v>>16;return float(v&0x00ffffffu)/16777216.0;}\n"
"kernel void path_preview(device const uchar4 *vox [[buffer(0)]],\n"
" device const int4 *table [[buffer(1)]],device float4 *accum [[buffer(2)]],\n"
" device uchar4 *out [[buffer(3)]],constant uint4 *u [[buffer(4)]],\n"
" constant int4 *bounds [[buffer(5)]],constant float4 *rays [[buffer(6)]],\n"
" uint2 gid [[thread_position_in_grid]]){uint w=u[0].x,h=u[0].y;\n"
" if(gid.x>=w||gid.y>=h)return;uint sample=u[0].z,cap=u[0].w;\n"
" uint maxSteps=u[1].x;uint idx=gid.y*w+gid.x;\n"
" uint seed=idx*9781u+sample*6271u+0x68bc21ebu;\n"
" float2 jitter=float2(random01(seed),random01(seed^0xa511e9b3u))-0.5;\n"
" float2 uv=(float2(gid)+0.5+jitter)/float2(w,h);\n"
" float3 ro=mix(mix(rays[0].xyz,rays[1].xyz,uv.x),\n"
"               mix(rays[2].xyz,rays[3].xyz,uv.x),uv.y);\n"
" float3 rd=normalize(mix(mix(rays[4].xyz,rays[5].xyz,uv.x),\n"
"                         mix(rays[6].xyz,rays[7].xyz,uv.x),uv.y));\n"
" uchar4 hit;float3 normal;int3 hitCell;float3 color=rays[9].xyz;\n"
" if(trace_vox(vox,table,cap,bounds[0].xyz,bounds[1].xyz,ro,rd,maxSteps,\n"
"              hit,normal,hitCell)){float3 base=pow(float3(hit.rgb)/255.0,float3(2.2));\n"
"  float ambient=rays[8].w;float light=max(dot(normalize(normal),\n"
"  normalize(rays[8].xyz)),0.0)*u[1].y/1000.0;\n"
"  uchar4 shadowHit;float3 shadowNormal;int3 shadowCell;\n"
"  float3 shadowOrigin=float3(hitCell)+0.5+normal*0.501;\n"
"  if(light>0.0&&trace_vox(vox,table,cap,bounds[0].xyz,bounds[1].xyz,\n"
"     shadowOrigin,normalize(rays[8].xyz),min(maxSteps,512u),shadowHit,\n"
"     shadowNormal,shadowCell))light*=0.2;\n"
"  color=base*clamp(ambient+light,0.0,2.0);}\n"
" float4 sum=sample==0?float4(color,1):accum[idx]+float4(color,1);\n"
" accum[idx]=sum;float3 avg=sum.xyz/sum.w;\n"
" out[idx]=uchar4(uchar3(clamp(pow(avg,float3(1.0/2.2))*255.0,\n"
"                              0.0,255.0)),255);}\n";

const int8_t *volume_mc_get_tri_table(void);

void gpu_accel_query_metal(gpu_capabilities_t *capabilities)
{
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device) {
        snprintf(capabilities->reason, sizeof(capabilities->reason),
                 "Metal device creation failed; CPU fallback active");
        return;
    }
    capabilities->available = true;
    capabilities->backend = GPU_BACKEND_METAL;
    snprintf(capabilities->device_name, sizeof(capabilities->device_name),
             "%s", device.name.UTF8String ?: "Apple GPU");
    snprintf(capabilities->reason, sizeof(capabilities->reason),
             "Metal sparse mirror and block-meshing pipelines available");
    // Feature flags remain false until their implementation phases pass.
    [device release];
}

void *gpu_accel_metal_mirror_create(size_t size)
{
    gpu_metal_mirror_t *mirror = calloc(1, sizeof(*mirror));
    if (!mirror) return NULL;
    mirror->device = MTLCreateSystemDefaultDevice();
    if (!mirror->device) {
        free(mirror);
        return NULL;
    }
    mirror->buffer = [mirror->device newBufferWithLength:size
                                                options:MTLResourceStorageModeShared];
    if (!mirror->buffer) {
        [mirror->device release];
        free(mirror);
        return NULL;
    }
    mirror->size = size;
    memset(mirror->buffer.contents, 0, size);
    mirror->queue = [mirror->device newCommandQueue];
    NSError *error = nil;
    id<MTLLibrary> library =
        [mirror->device newLibraryWithSource:BLOCK_MESH_SOURCE
                                     options:nil error:&error];
    id<MTLFunction> function =
        [library newFunctionWithName:@"count_block_faces"];
    if (function)
        mirror->face_count_pipeline =
            [mirror->device newComputePipelineStateWithFunction:function
                                                          error:&error];
    mirror->mesh_input =
        [mirror->device newBufferWithLength:18 * 18 * 18 * 4
                                    options:MTLResourceStorageModeShared];
    mirror->mesh_count =
        [mirror->device newBufferWithLength:sizeof(uint32_t)
                                    options:MTLResourceStorageModeShared];
    [function release];
    [library release];
    error = nil;
    library = [mirror->device newLibraryWithSource:BLOCK_EMIT_SOURCE
                                           options:nil error:&error];
    function = [library newFunctionWithName:@"emit_block_faces"];
    if (function)
        mirror->face_emit_pipeline =
            [mirror->device newComputePipelineStateWithFunction:function
                                                          error:&error];
    mirror->mesh_faces =
        [mirror->device newBufferWithLength:24576 * sizeof(uint32_t)
                                    options:MTLResourceStorageModeShared];
    [function release];
    [library release];
    error = nil;
    library = [mirror->device newLibraryWithSource:BLOCK_VERTEX_SOURCE
                                           options:nil error:&error];
    function = [library newFunctionWithName:@"emit_block_vertices"];
    if (function)
        mirror->vertex_emit_pipeline =
            [mirror->device newComputePipelineStateWithFunction:function
                                                          error:&error];
    mirror->mesh_vertices =
        [mirror->device newBufferWithLength:24576 * 4 * 36
                                    options:MTLResourceStorageModeShared];
    [function release];
    [library release];
    error = nil;
    library = [mirror->device newLibraryWithSource:MC_VERTEX_SOURCE
                                           options:nil error:&error];
    function = [library newFunctionWithName:@"emit_mc_vertices"];
    if (function)
        mirror->mc_emit_pipeline =
            [mirror->device newComputePipelineStateWithFunction:function
                                                          error:&error];
    mirror->mc_tri_table =
        [mirror->device newBufferWithLength:256 * 16
                                    options:MTLResourceStorageModeShared];
    if (mirror->mc_tri_table)
        memcpy(mirror->mc_tri_table.contents, volume_mc_get_tri_table(),
               256 * 16);
    [function release];
    [library release];
    error = nil;
    library = [mirror->device newLibraryWithSource:PATH_PREVIEW_SOURCE
                                           options:nil error:&error];
    function = [library newFunctionWithName:@"path_preview"];
    if (function)
        mirror->path_preview_pipeline =
            [mirror->device newComputePipelineStateWithFunction:function
                                                          error:&error];
    [function release];
    [library release];
    if (!mirror->queue || !mirror->face_count_pipeline ||
            !mirror->face_emit_pipeline || !mirror->mesh_input ||
            !mirror->mesh_count || !mirror->mesh_faces ||
            !mirror->vertex_emit_pipeline || !mirror->mesh_vertices ||
            !mirror->mc_emit_pipeline || !mirror->mc_tri_table ||
            !mirror->path_preview_pipeline) {
        gpu_accel_metal_mirror_destroy(mirror);
        return NULL;
    }
    return mirror;
}

void gpu_accel_metal_mirror_destroy(void *ptr)
{
    gpu_metal_mirror_t *mirror = ptr;
    if (!mirror) return;
    [mirror->mc_tri_table release];
    [mirror->mesh_vertices release];
    [mirror->mesh_faces release];
    [mirror->mesh_count release];
    [mirror->mesh_input release];
    [mirror->face_emit_pipeline release];
    [mirror->vertex_emit_pipeline release];
    [mirror->mc_emit_pipeline release];
    [mirror->path_preview_pipeline release];
    [mirror->path_output release];
    [mirror->path_accum release];
    [mirror->path_hash release];
    [mirror->face_count_pipeline release];
    [mirror->queue release];
    [mirror->buffer release];
    [mirror->device release];
    free(mirror);
}

bool gpu_accel_metal_count_block_faces(void *ptr, const void *voxels,
                                       uint32_t *face_count)
{
    gpu_metal_mirror_t *mirror = ptr;
    id<MTLCommandBuffer> command;
    id<MTLComputeCommandEncoder> encoder;
    if (!mirror || !voxels || !face_count) return false;
    memcpy(mirror->mesh_input.contents, voxels, 18 * 18 * 18 * 4);
    *(uint32_t *)mirror->mesh_count.contents = 0;
    command = [mirror->queue commandBuffer];
    encoder = [command computeCommandEncoder];
    [encoder setComputePipelineState:mirror->face_count_pipeline];
    [encoder setBuffer:mirror->mesh_input offset:0 atIndex:0];
    [encoder setBuffer:mirror->mesh_count offset:0 atIndex:1];
    [encoder dispatchThreads:MTLSizeMake(4096, 1, 1)
       threadsPerThreadgroup:MTLSizeMake(
           MIN((NSUInteger)256,
               mirror->face_count_pipeline.maxTotalThreadsPerThreadgroup),
           1, 1)];
    [encoder endEncoding];
    [command commit];
    [command waitUntilCompleted];
    if (command.status != MTLCommandBufferStatusCompleted) return false;
    *face_count = *(uint32_t *)mirror->mesh_count.contents;
    return true;
}

bool gpu_accel_metal_emit_block_faces(void *ptr, const void *voxels,
                                      uint32_t *faces, uint32_t capacity,
                                      uint32_t *face_count)
{
    gpu_metal_mirror_t *mirror = ptr;
    id<MTLCommandBuffer> command;
    id<MTLComputeCommandEncoder> encoder;
    uint32_t count;
    if (!mirror || !voxels || !faces || !face_count || !capacity)
        return false;
    memcpy(mirror->mesh_input.contents, voxels, 18 * 18 * 18 * 4);
    *(uint32_t *)mirror->mesh_count.contents = 0;
    command = [mirror->queue commandBuffer];
    encoder = [command computeCommandEncoder];
    [encoder setComputePipelineState:mirror->face_emit_pipeline];
    [encoder setBuffer:mirror->mesh_input offset:0 atIndex:0];
    [encoder setBuffer:mirror->mesh_count offset:0 atIndex:1];
    [encoder setBuffer:mirror->mesh_faces offset:0 atIndex:2];
    [encoder setBytes:&capacity length:sizeof(capacity) atIndex:3];
    [encoder dispatchThreads:MTLSizeMake(4096, 1, 1)
       threadsPerThreadgroup:MTLSizeMake(
           MIN((NSUInteger)256,
               mirror->face_emit_pipeline.maxTotalThreadsPerThreadgroup),
           1, 1)];
    [encoder endEncoding];
    [command commit];
    [command waitUntilCompleted];
    if (command.status != MTLCommandBufferStatusCompleted) return false;
    count = *(uint32_t *)mirror->mesh_count.contents;
    *face_count = count;
    if (count > capacity) return false;
    memcpy(faces, mirror->mesh_faces.contents, count * sizeof(*faces));
    return true;
}

bool gpu_accel_metal_emit_block_vertices(void *ptr, const void *voxels,
                                         void *vertices, uint32_t capacity,
                                         uint32_t *face_count)
{
    gpu_metal_mirror_t *mirror = ptr;
    id<MTLCommandBuffer> command;
    id<MTLComputeCommandEncoder> encoder;
    uint32_t count;
    if (!mirror || !voxels || !vertices || !face_count || !capacity)
        return false;
    memcpy(mirror->mesh_input.contents, voxels, 18 * 18 * 18 * 4);
    *(uint32_t *)mirror->mesh_count.contents = 0;
    command = [mirror->queue commandBuffer];
    encoder = [command computeCommandEncoder];
    [encoder setComputePipelineState:mirror->vertex_emit_pipeline];
    [encoder setBuffer:mirror->mesh_input offset:0 atIndex:0];
    [encoder setBuffer:mirror->mesh_count offset:0 atIndex:1];
    [encoder setBuffer:mirror->mesh_vertices offset:0 atIndex:2];
    [encoder setBytes:&capacity length:sizeof(capacity) atIndex:3];
    [encoder dispatchThreads:MTLSizeMake(4096, 1, 1)
       threadsPerThreadgroup:MTLSizeMake(
           MIN((NSUInteger)256,
               mirror->vertex_emit_pipeline.maxTotalThreadsPerThreadgroup),
           1, 1)];
    [encoder endEncoding];
    [command commit];
    [command waitUntilCompleted];
    if (command.status != MTLCommandBufferStatusCompleted) return false;
    count = *(uint32_t *)mirror->mesh_count.contents;
    *face_count = count;
    if (count > capacity) return false;
    memcpy(vertices, mirror->mesh_vertices.contents, count * 4 * 36);
    return true;
}

bool gpu_accel_metal_emit_mc_vertices(void *ptr, const void *voxels,
                                      void *vertices, uint32_t capacity,
                                      uint32_t *triangle_count)
{
    gpu_metal_mirror_t *mirror = ptr;
    id<MTLCommandBuffer> command;
    id<MTLComputeCommandEncoder> encoder;
    uint32_t count;
    if (!mirror || !voxels || !vertices || !triangle_count || !capacity)
        return false;
    memcpy(mirror->mesh_input.contents, voxels, 18 * 18 * 18 * 4);
    *(uint32_t *)mirror->mesh_count.contents = 0;
    command = [mirror->queue commandBuffer];
    encoder = [command computeCommandEncoder];
    [encoder setComputePipelineState:mirror->mc_emit_pipeline];
    [encoder setBuffer:mirror->mesh_input offset:0 atIndex:0];
    [encoder setBuffer:mirror->mesh_count offset:0 atIndex:1];
    [encoder setBuffer:mirror->mesh_vertices offset:0 atIndex:2];
    [encoder setBytes:&capacity length:sizeof(capacity) atIndex:3];
    [encoder setBuffer:mirror->mc_tri_table offset:0 atIndex:4];
    [encoder dispatchThreads:MTLSizeMake(4096, 1, 1)
       threadsPerThreadgroup:MTLSizeMake(
           MIN((NSUInteger)256,
               mirror->mc_emit_pipeline.maxTotalThreadsPerThreadgroup),1,1)];
    [encoder endEncoding];
    [command commit];
    [command waitUntilCompleted];
    if (command.status != MTLCommandBufferStatusCompleted) return false;
    count = *(uint32_t *)mirror->mesh_count.contents;
    *triangle_count = count;
    if (count > capacity) return false;
    memcpy(vertices, mirror->mesh_vertices.contents, count * 3 * 36);
    return true;
}

bool gpu_accel_metal_render_path_preview(
        void *ptr, const int32_t *hash_table, uint32_t hash_capacity,
        const int32_t bounds[2][4],
        const gpu_path_preview_params_t *params, uint8_t *rgba)
{
    gpu_metal_mirror_t *mirror = ptr;
    id<MTLCommandBuffer> command;
    id<MTLComputeCommandEncoder> encoder;
    uint32_t uniforms[8] = {};
    float rays[10][4] = {};
    size_t pixels, hash_bytes;
    int i;
    if (!mirror || !hash_table || !hash_capacity || !bounds || !params ||
            !rgba || params->width <= 0 || params->height <= 0 ||
            params->max_steps <= 0)
        return false;
    if ((size_t)params->width > SIZE_MAX / (size_t)params->height)
        return false;
    pixels = (size_t)params->width * (size_t)params->height;
    if (pixels > (size_t)2048 * 2048)
        return false;
    hash_bytes = hash_capacity * 4 * sizeof(int32_t);
    if (mirror->path_hash_size < hash_bytes) {
        [mirror->path_hash release];
        mirror->path_hash = [mirror->device newBufferWithLength:hash_bytes
                                    options:MTLResourceStorageModeShared];
        mirror->path_hash_size = mirror->path_hash ? hash_bytes : 0;
    }
    if (mirror->path_pixel_capacity < pixels) {
        [mirror->path_accum release];
        [mirror->path_output release];
        mirror->path_accum = [mirror->device
            newBufferWithLength:pixels * 4 * sizeof(float)
                         options:MTLResourceStorageModeShared];
        mirror->path_output = [mirror->device
            newBufferWithLength:pixels * 4
                         options:MTLResourceStorageModeShared];
        mirror->path_pixel_capacity =
            mirror->path_accum && mirror->path_output ? pixels : 0;
    }
    if (!mirror->path_hash || !mirror->path_accum || !mirror->path_output)
        return false;
    memcpy(mirror->path_hash.contents, hash_table, hash_bytes);
    uniforms[0] = (uint32_t)params->width;
    uniforms[1] = (uint32_t)params->height;
    uniforms[2] = (uint32_t)params->sample;
    uniforms[3] = hash_capacity;
    uniforms[4] = (uint32_t)params->max_steps;
    float light = params->light_intensity * 1000.0f;
    if (light < 0.0f) light = 0.0f;
    if (light > 10000.0f) light = 10000.0f;
    uniforms[5] = (uint32_t)light;
    for (i = 0; i < 4; i++) {
        memcpy(rays[i], params->ray_origins[i], sizeof(rays[i]));
        memcpy(rays[4 + i], params->ray_directions[i], sizeof(rays[i]));
    }
    memcpy(rays[8], params->light_direction, sizeof(rays[8]));
    rays[8][3] = params->ambient;
    memcpy(rays[9], params->background, sizeof(rays[9]));
    command = [mirror->queue commandBuffer];
    encoder = [command computeCommandEncoder];
    [encoder setComputePipelineState:mirror->path_preview_pipeline];
    [encoder setBuffer:mirror->buffer offset:0 atIndex:0];
    [encoder setBuffer:mirror->path_hash offset:0 atIndex:1];
    [encoder setBuffer:mirror->path_accum offset:0 atIndex:2];
    [encoder setBuffer:mirror->path_output offset:0 atIndex:3];
    [encoder setBytes:uniforms length:sizeof(uniforms) atIndex:4];
    [encoder setBytes:bounds length:sizeof(int32_t) * 8 atIndex:5];
    [encoder setBytes:rays length:sizeof(rays) atIndex:6];
    [encoder dispatchThreads:MTLSizeMake(params->width, params->height, 1)
       threadsPerThreadgroup:MTLSizeMake(8, 8, 1)];
    [encoder endEncoding];
    [command commit];
    [command waitUntilCompleted];
    if (command.status != MTLCommandBufferStatusCompleted) return false;
    memcpy(rgba, mirror->path_output.contents, pixels * 4);
    return true;
}

bool gpu_accel_metal_mirror_upload(void *ptr, size_t offset,
                                   const void *data, size_t size)
{
    gpu_metal_mirror_t *mirror = ptr;
    if (!mirror || !data || offset > mirror->size ||
            size > mirror->size - offset)
        return false;
    memcpy((uint8_t *)mirror->buffer.contents + offset, data, size);
    return true;
}

uint64_t gpu_accel_metal_mirror_checksum(void *ptr, size_t offset, size_t size)
{
    gpu_metal_mirror_t *mirror = ptr;
    const uint8_t *data;
    uint64_t hash = UINT64_C(1469598103934665603);
    size_t i;
    if (!mirror || offset > mirror->size || size > mirror->size - offset)
        return 0;
    data = (const uint8_t *)mirror->buffer.contents + offset;
    for (i = 0; i < size; i++) {
        hash ^= data[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}
