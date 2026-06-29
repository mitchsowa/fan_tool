// Offline Modbus-RTU stream parser with CRC resync. Reads a raw byte dump
// (from capture) and reconstructs the true frame boundaries by walking the
// stream and accepting only CRC-valid frames of recognized shapes, pairing
// each slave response with the preceding request.
//   parse <raw.bin>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

static uint16_t crc16(const uint8_t* p, size_t n) {
    uint16_t c = 0xFFFF;
    for (size_t i = 0; i < n; ++i) { c ^= p[i];
        for (int b=0;b<8;++b) c=(c&1)?(c>>1)^0xA001:(c>>1); }
    return c;
}
static bool crc_ok(const uint8_t* p, size_t n) {
    if (n < 4) return false;
    uint16_t got = p[n-2] | (p[n-1]<<8);
    return got == crc16(p, n-2);
}
static uint16_t be16(const uint8_t* p){ return (p[0]<<8)|p[1]; }
static const char* fname(uint8_t f){ switch(f&0x7F){
    case 1:return "ReadCoils"; case 2:return "ReadDiscrete";
    case 3:return "ReadHolding"; case 4:return "ReadInput";
    case 5:return "WriteCoil"; case 6:return "WriteSingle";
    case 16:return "WriteMultiple"; default:return "Func?"; } }

// Candidate frame at position i. Returns length consumed (0 = no valid frame).
// `expect_resp` biases ambiguous FC03/04 toward response form.
static size_t try_frame(const std::vector<uint8_t>& d, size_t i,
                        bool expect_resp, bool& is_resp) {
    size_t avail = d.size() - i;
    const uint8_t* p = &d[i];
    uint8_t func = (avail >= 2) ? p[1] : 0;
    uint8_t base = func & 0x7F;

    // Exception response: addr, func|0x80, code, crc = 5 bytes.
    if ((func & 0x80) && avail >= 5 && crc_ok(p, 5)) { is_resp = true; return 5; }

    auto resp0304 = [&]() -> size_t {            // addr,fn,bc,data[bc],crc
        if (avail >= 3) { uint8_t bc = p[2]; size_t L = 3 + bc + 2;
            if (bc >= 1 && avail >= L && crc_ok(p, L)) return L; }
        return 0; };
    auto req0304 = [&]() -> size_t {              // 8-byte read request
        if (avail >= 8 && crc_ok(p, 8)) return 8; return 0; };

    if (base == 3 || base == 4) {
        size_t a = resp0304(), b = req0304();
        if (a && b) { if (expect_resp) { is_resp=true; return a; }
                      is_resp=false; return b; }
        if (a) { is_resp=true; return a; }
        if (b) { is_resp=false; return b; }
        return 0;
    }
    if (base == 6 || base == 5) {                 // write single: 8B, echo
        if (avail >= 8 && crc_ok(p, 8)) { is_resp = expect_resp; return 8; }
        return 0;
    }
    if (base == 16) {
        // response: addr,10,reg,reg,qty,qty,crc = 8B
        if (avail >= 8 && crc_ok(p, 8)) { is_resp=true; return 8; }
        // request: addr,10,reg,reg,qty,qty,bc,data,crc
        if (avail >= 7) { uint8_t bc=p[6]; size_t L=7+bc+2;
            if (avail>=L && crc_ok(p,L)) { is_resp=false; return L; } }
        return 0;
    }
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: parse <raw.bin>\n"); return 1; }
    FILE* f = fopen(argv[1], "rb"); if(!f){ perror("open"); return 1; }
    std::vector<uint8_t> d; uint8_t b[4096]; size_t r;
    while ((r=fread(b,1,sizeof(b),f))>0) d.insert(d.end(),b,b+r);
    fclose(f);

    printf("Parsed %zu raw bytes\n\n", d.size());
    size_t i = 0, frames = 0, skipped = 0;
    bool expect_resp = false;          // toggles after each accepted frame
    uint16_t last_reg = 0, last_qty = 0; uint8_t last_fn = 0;
    while (i < d.size()) {
        bool is_resp = false;
        size_t L = try_frame(d, i, expect_resp, is_resp);
        if (L == 0) { ++i; ++skipped; continue; }   // resync: drop one byte
        const uint8_t* p = &d[i];
        uint8_t addr = p[0], func = p[1], base = func & 0x7F;
        printf("%-4s addr=%-3u %-13s %2zuB | ", is_resp?"RSP":"REQ", addr, fname(func), L);
        if (func & 0x80) {
            printf("EXCEPTION code=%u", p[2]);
        } else if ((base==3||base==4) && !is_resp) {
            printf("read reg=%u qty=%u", be16(&p[2]), be16(&p[4]));
            last_reg=be16(&p[2]); last_qty=be16(&p[4]); last_fn=base;
        } else if ((base==3||base==4) && is_resp) {
            printf("bytes=%u ->", p[2]);
            for (uint8_t k=0;k+1<p[2];k+=2) printf(" [%u]=%u", last_reg + k/2, be16(&p[3+k]));
        } else if (base==6) {
            printf("WRITE reg=%u val=%u (0x%04X)", be16(&p[2]), be16(&p[4]), be16(&p[4]));
        } else if (base==16 && !is_resp) {
            printf("write-multi reg=%u qty=%u", be16(&p[2]), be16(&p[4]));
        } else if (base==16 && is_resp) {
            printf("write-multi-ack reg=%u qty=%u", be16(&p[2]), be16(&p[4]));
        }
        printf("\n");
        (void)last_qty; (void)last_fn;
        i += L; ++frames;
        // Heuristic: a request is followed by a response and vice versa.
        expect_resp = !is_resp;
    }
    printf("\n== %zu frames decoded, %zu bytes skipped (resync) ==\n", frames, skipped);
    return 0;
}
