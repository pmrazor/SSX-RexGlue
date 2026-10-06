// FXC reference for the typed ROV/half sequence. Xbox factors 8 and 10
// deliberately differ: destination COLOR versus destination ALPHA.
cbuffer Constants : register(b0) { uint4 layer_parameters; uint destination; };
RasterizerOrderedBuffer<uint> edram : register(u1);
float3 Factor(uint f,float4 source,float destination_alpha) {
  switch(f) {
    case 1:return 1;
    case 4:return source.rgb;
    case 5:return 1-source.rgb;
    case 6:return source.a;
    case 7:return 1-source.a;
    case 10:return destination_alpha;
    case 11:return 1-destination_alpha;
    default:return 0;
  }
}
float4 LoadHalf(uint address) {
  uint2 p=uint2(edram[address],edram[address+1]);
  return f16tof32(uint4(p.x&65535,p.x>>16,p.y&65535,p.y>>16));
}
void StoreHalf(uint address,float4 value) {
  uint4 h=f32tof16(clamp(value,0,65504));
  edram[address]=h.x|(h.y<<16);edram[address+1]=h.z|(h.w<<16);
}
void main(float4 source:TEXCOORD0,float4 position:SV_Position) {
  if(!layer_parameters.x)return;
  uint2 xy=uint2(position.xy);
  if(any(xy>=layer_parameters.yz))return;
  uint c_address=layer_parameters.x+2*(xy.y*3840+xy.x);
  uint t_address=c_address+3840*2160*2;
  float4 c=LoadHalf(c_address),t=LoadHalf(t_address);
  float alpha=float(edram[destination]>>24)/255;
  uint fs=layer_parameters.w&31,fd=(layer_parameters.w>>8)&31;
  float3 add=source.rgb*Factor(fs,source,alpha);
  float3 gain=Factor(fd,source,alpha)+(fs==8?source.rgb:0);
  c.rgb=add+c.rgb*gain;t.rgb*=gain;
  StoreHalf(c_address,c);StoreHalf(t_address,t);
}
