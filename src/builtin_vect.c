#include "builtin_funcs.h"
#include "lavender.h"
#include "textbuffer.h"
#include "builtin.h"
#include "expression.h"
#include <assert.h>

#define INTRINSIC(name) \
    TextBufferObj lv_vect_##name(TextBufferObj* args)

INTRINSIC(cat) {
    TextBufferObj res;
    assert(args[0].type == OPT_VECT);
    assert(args[1].type == OPT_VECT);
    size_t alen = args[0].vect->len;
    size_t blen = args[1].vect->len;
    LvVect* vec = lv_alloc(sizeof(LvVect) + (alen + blen) * sizeof(TextBufferObj));
    vec->refCount = 0;
    vec->len = alen + blen;
    for(size_t i = 0; i < alen; i++) {
        vec->data[i] = args[0].vect->data[i];
        INC_REFCOUNT(&vec->data[i]);
    }
    for(size_t i = 0; i < blen; i++) {
        vec->data[alen + i] = args[1].vect->data[i];
        INC_REFCOUNT(&vec->data[alen + i]);
    }
    res.type = OPT_VECT;
    res.vect = vec;
    return res;
}

INTRINSIC(at) {
    TextBufferObj res;
    assert(args[1].type == OPT_VECT);
    if(args[0].type == OPT_INTEGER && args[0].integer < args[1].vect->len) {
        res = args[1].vect->data[(size_t)args[0].integer];
    } else {
        res.type = OPT_UNDEFINED;
    }
    return res;
}

INTRINSIC(eq) {
    TextBufferObj res;
    assert(args[0].type == OPT_VECT);
    assert(args[1].type == OPT_VECT);
    res.type = OPT_INTEGER;
    if(args[0].vect->len != args[1].vect->len) {
        res.integer = 0;
    } else {
        bool equal = true;
        for(size_t i = 0; i < equal && args[0].vect->len; i++) {
            equal = lv_blt_equal(&args[0].vect->data[i], &args[1].vect->data[i]);
        }
        res.integer = equal;
    }
    return res;
}

INTRINSIC(lt) {
    TextBufferObj res;
    assert(args[0].type == OPT_VECT);
    assert(args[1].type == OPT_VECT);
    if(args[0].vect->len == args[1].vect->len) {
        int cmp = 0;
        for(size_t i = 0; (cmp == 0) && i < args[0].vect->len; i++) {
            if(lv_blt_lt(&args[0].vect->data[i], &args[1].vect->data[i])) {
                cmp = -1;
            } else if(lv_blt_lt(&args[1].vect->data[i], &args[0].vect->data[i])) {
                cmp = 1;
            }
        }
        res.integer = cmp == -1;
    }
    res.integer = args[0].vect->len < args[1].vect->len;
    return res;
}

INTRINSIC(map) {
    TextBufferObj res;
    assert(args[0].type == OPT_VECT);
    TextBufferObj func = args[1]; //in case the stack is reallocated
    TextBufferObj* oldData = args[0].vect->data;
    size_t len = args[0].vect->len;
    LvVect* vect = lv_alloc(sizeof(LvVect) + len * sizeof(TextBufferObj));
    vect->refCount = 0;
    vect->len = len;
    for(size_t i = 0; i < len; i++) {
        TextBufferObj obj;
        lv_callFunction(&func, 1, &oldData[i], &obj);
        INC_REFCOUNT(&obj);
        vect->data[i] = obj;
    }
    res.type = OPT_VECT;
    res.vect = vect;
    return res;
}

INTRINSIC(filter) {
    TextBufferObj res;
    assert(args[0].type == OPT_VECT);
    TextBufferObj func = args[1];
    TextBufferObj* oldData = args[0].vect->data;
    size_t len = args[0].vect->len;
    LvVect* vect = lv_alloc(sizeof(LvVect) + len * sizeof(TextBufferObj));
    vect->refCount = 0;
    size_t newLen = 0;
    for(size_t i = 0; i < len; i++) {
        TextBufferObj passed;
        lv_callFunction(&func, 1, &oldData[i], &passed);
        INC_REFCOUNT(&passed); //so lv_expr_cleanup doesn't blow up
        if(lv_blt_toBool(&passed)) {
            INC_REFCOUNT(&oldData[i]);
            vect->data[newLen] = oldData[i];
            newLen++;
        }
        lv_expr_cleanup(&passed, 1);
    }
    vect->len = newLen;
    if(newLen < len) {
        vect = lv_realloc(vect, sizeof(LvVect) + newLen * sizeof(TextBufferObj));
    }
    res.type = OPT_VECT;
    res.vect = vect;
    return res;
}

INTRINSIC(fold) {
    TextBufferObj res;
    assert(args[0].type == OPT_VECT);
    size_t len = args[0].vect->len;
    TextBufferObj* oldData = args[0].vect->data;
    TextBufferObj accum[2] = { args[1] };
    TextBufferObj func = args[2];
    for(size_t i = 0; i < len; i++) {
        accum[1] = oldData[i];
        lv_callFunction(&func, 2, accum, &accum[0]);
    }
    res = accum[0];
    return res;
}

INTRINSIC(slice) {
    TextBufferObj res;
    assert(args[0].type == OPT_VECT);
    if(args[1].type == OPT_INTEGER && args[2].type == OPT_INTEGER) {
        uint64_t start = args[1].integer;
        uint64_t end = args[2].integer;
        size_t len = args[0].vect->len;
        //sanity check
        if(!NEGATIVE_INT(start) && start < end && (size_t)end < len) {
            //create new vect
            res.type = OPT_VECT;
            res.vect = lv_alloc(sizeof(LvVect) + (end - start) * sizeof(TextBufferObj));
            res.vect->refCount = 0;
            res.vect->len = end - start;
            //copy over elements
            for(size_t i = 0; i < res.vect->len; i++) {
                res.vect->data[i] = args[0].vect->data[start + i];
                INC_REFCOUNT(&res.vect->data[i]);
            }
        } else {
            res.type = OPT_UNDEFINED;
        }
    } else {
        res.type = OPT_UNDEFINED;
    }
    return res;
}

INTRINSIC(take) {
    TextBufferObj res;
    assert(args[0].type == OPT_VECT);
    TextBufferObj func = args[1];
    TextBufferObj* data = args[0].vect->data;
    size_t len = args[0].vect->len;
    //overestimate
    LvVect* vec = lv_alloc(sizeof(LvVect) + args[0].vect->len * sizeof(TextBufferObj));
    vec->refCount = 0;
    size_t newLen = 0;
    bool cont = true;
    while(cont && newLen < len) {
        TextBufferObj satisfied;
        lv_callFunction(&func, 1, &data[newLen], &satisfied);
        INC_REFCOUNT(&satisfied);
        if(lv_blt_toBool(&satisfied)) {
            INC_REFCOUNT(&data[newLen]);
            vec->data[newLen] = data[newLen];
            newLen++;
        } else {
            cont = false;
        }
        lv_expr_cleanup(&satisfied, 1);
    }
    vec = lv_realloc(vec, sizeof(LvVect) + newLen * sizeof(TextBufferObj));
    vec->len = newLen;
    res.type = OPT_VECT;
    res.vect = vec;
    return res;
}

INTRINSIC(skip) {
    TextBufferObj res;
    assert(args[0].type == OPT_VECT);
    TextBufferObj func = args[1];
    TextBufferObj* data = args[0].vect->data;
    size_t len = args[0].vect->len;
    size_t skipLen = 0;
    bool cont = true;
    while(cont && skipLen < len) {
        TextBufferObj satisfied;
        lv_callFunction(&func, 1, &data[skipLen], &satisfied);
        INC_REFCOUNT(&satisfied);
        if(lv_blt_toBool(&satisfied)) {
            skipLen++;
        } else {
            cont = false;
        }
        lv_expr_cleanup(&satisfied, 1);
    }
    LvVect* vec = lv_alloc(sizeof(LvVect) + (len - skipLen) * sizeof(TextBufferObj));
    vec->refCount = 0;
    vec->len = len - skipLen;
    for(size_t i = skipLen; i < len; i++) {
        INC_REFCOUNT(&data[i]);
        vec->data[i - skipLen] = data[i];
    }
    res.type = OPT_VECT;
    res.vect = vec;
    return res;
}
