// TEST DOUBLE for host validation only. NOT the official Expert Sleepers API.
// Same public interface as distingnt/serialisation.h. Values written with
// _NT_jsonStream become a flat token list (like a jsmn-style parser would
// produce) which _NT_jsonParse then walks. No standard library: the plugin
// under test defines its own placement new.
#pragma once
#include <stdint.h>
#include <string.h>

namespace nt_json_test {
enum { kObj, kArr, kName, kNum, kFlt, kStr, kBool, kNull };
struct Tok { int type, ival, size, end, str; float fval; };
inline Tok toks[16384];
inline int ntok = 0;
inline char pool[1 << 19];
inline int npool = 0;
inline int stack[64];
inline int sp = 0;
inline long textBytes = 0;   // approximate size of the equivalent JSON text
inline int add(int type) {
    Tok& t = toks[ntok];
    t.type = type; t.ival = 0; t.size = 0; t.end = ntok + 1; t.str = -1; t.fval = 0;
    // Values inside an array count as elements; names count as members.
    if (sp > 0) {
        Tok& parent = toks[stack[sp - 1]];
        if ((parent.type == kArr && type != kName) || (parent.type == kObj && type == kName)) {
            ++parent.size;
            if (parent.size > 1) ++textBytes;   // comma
        }
    }
    return ntok++;
}
inline int addStr(const char* s) {
    const int at = npool;
    const int n = (int)strlen(s);
    memcpy(pool + npool, s, n + 1);
    npool += n + 1;
    textBytes += n + 2;
    return at;
}
inline void reset() { ntok = 0; npool = 0; sp = 0; textBytes = 0; }
}

class _NT_jsonStream {
public:
    // The host calls serialise() inside an object: open a root object here.
    explicit _NT_jsonStream(void*) {
        using namespace nt_json_test;
        reset();
        stack[sp++] = add(kObj);
        textBytes += 1;
    }
    void finish() { closeObject(); }
    void openArray(void)  { using namespace nt_json_test; stack[sp++] = add(kArr); textBytes += 1; }
    void closeArray(void) { using namespace nt_json_test; toks[stack[--sp]].end = ntok; textBytes += 1; }
    void openObject(void) { using namespace nt_json_test; stack[sp++] = add(kObj); textBytes += 1; }
    void closeObject(void){ using namespace nt_json_test; toks[stack[--sp]].end = ntok; textBytes += 1; }
    void addMemberName(const char* str) {
        using namespace nt_json_test;
        const int t = add(kName); toks[t].str = addStr(str); textBytes += 1;
    }
    void addNumber(int v)   { using namespace nt_json_test; const int t = add(kNum); toks[t].ival = v; textBytes += 6; }
    void addNumber(float v) { using namespace nt_json_test; const int t = add(kFlt); toks[t].fval = v; textBytes += 10; }
    void addString(const char* str) { using namespace nt_json_test; const int t = add(kStr); toks[t].str = addStr(str); }
    void addFourCC(uint32_t) { using namespace nt_json_test; const int t = add(kStr); toks[t].str = addStr("xxxx"); }
    void addBoolean(bool v) { using namespace nt_json_test; const int t = add(kBool); toks[t].ival = v; textBytes += 5; }
    void addNull(void)      { using namespace nt_json_test; add(kNull); textBytes += 4; }
};

class _NT_jsonParse {
    int i;
public:
    _NT_jsonParse(void*, int start) : i(start) {}
    bool numberOfArrayElements(int& num) {
        using namespace nt_json_test;
        if (i >= ntok || toks[i].type != kArr) return false;
        num = toks[i].size; ++i; return true;
    }
    bool numberOfObjectMembers(int& num) {
        using namespace nt_json_test;
        if (i >= ntok || toks[i].type != kObj) return false;
        num = toks[i].size; ++i; return true;
    }
    bool matchName(const char* name) {
        using namespace nt_json_test;
        if (i >= ntok || toks[i].type != kName || strcmp(pool + toks[i].str, name) != 0) return false;
        ++i; return true;
    }
    bool skipMember(void) {
        using namespace nt_json_test;
        if (i >= ntok || toks[i].type != kName) return false;
        ++i;
        if (i >= ntok) return false;
        i = toks[i].end;
        return true;
    }
    bool number(int& v) {
        using namespace nt_json_test;
        if (i >= ntok) return false;
        if (toks[i].type == kNum) v = toks[i].ival;
        else if (toks[i].type == kFlt) v = (int)toks[i].fval;
        else return false;
        ++i; return true;
    }
    bool number(float& v) {
        using namespace nt_json_test;
        if (i >= ntok) return false;
        if (toks[i].type == kNum) v = (float)toks[i].ival;
        else if (toks[i].type == kFlt) v = toks[i].fval;
        else return false;
        ++i; return true;
    }
    bool string(const char*& str) {
        using namespace nt_json_test;
        if (i >= ntok || toks[i].type != kStr) return false;
        str = pool + toks[i].str; ++i; return true;
    }
    bool boolean(bool& v) {
        using namespace nt_json_test;
        if (i >= ntok || toks[i].type != kBool) return false;
        v = toks[i].ival != 0; ++i; return true;
    }
    bool null(void) {
        using namespace nt_json_test;
        if (i >= ntok || toks[i].type != kNull) return false;
        ++i; return true;
    }
};
