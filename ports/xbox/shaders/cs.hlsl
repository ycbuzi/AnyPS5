RWByteAddressBuffer output : register(u0);

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    output.Store(id.x * 4, id.x * 3 + WaveGetLaneCount());
}
