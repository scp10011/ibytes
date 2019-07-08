#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <Python.h>
#include <stddef.h>

#define IBytesObject_SIZE  (sizeof(IBytesObject))
#define PY_SSIZE_T_CLEAN

const char *tp_name = "ibytes";
#define IBytes_Check(x)             ( (Py_TYPE(x)->tp_name ) ==  tp_name )
#define IBytes_SIZE(self)           (self->length-self->index)
#define IBytes_Data(self)           (self->base+self->index)

typedef struct {
    PyObject_HEAD;
    Py_ssize_t index;
    Py_ssize_t length;
    char *base;
} IBytesObject;


PyAPI_DATA(PyTypeObject)
        IBytesType;


int bytes_search(char *a, size_t al, char *b, size_t bl) {
    static size_t table[UCHAR_MAX + 1];
    size_t i = 0, len = al;
    for (i = 0; i <= UCHAR_MAX; i++)                      /* rdg 10/93 */
        table[i] = len;
    for (i = 0; i < len; i++)
        table[a[i]] = len - i - 1;
    char *findme = a;

    register size_t shift = 0;
    register size_t pos = len - 1;
    while (pos < bl) {
        while (pos < bl &&
               (shift = table[(char) b[pos]]) > 0) {
            pos += shift;
        }
        if (0 == shift) {
            if (0 == memcmp(findme, b + (pos - len + 1), len)) {
                return pos - len + 1;
            } else pos++;
        }
    }
    return -1;
}


static IBytesObject *ibytes_null;

static void
IBytes_dealloc(IBytesObject *self) {
    if (self->base != NULL) {
        PyObject_Free(self->base);
        self->base = NULL;
    }
    Py_TYPE(self)->tp_free(self);
}

PyObject *
to_bytes(IBytesObject *self) {
    Py_ssize_t newsize = 3, squotes = 0, dquotes = 0;
    PyObject *v;
    char quote, *s = self->base, *p;
    size_t i;
    for (i = self->index; i < self->length; i++) {
        Py_ssize_t incr = 1;
        switch (s[i]) {
            case '\'':
                squotes++;
                break;
            case '"':
                dquotes++;
                break;
            case '\\':
            case '\t':
            case '\n':
            case '\r':
                incr = 2;
                break; /* \C */
            default:
                if (s[i] < 0x20 || s[i] > 0x7e)
                    incr = 4; /* \xHH */
        }
        if (newsize > PY_SSIZE_T_MAX - incr)
            goto overflow;
        newsize += incr;
    }
    quote = '\'';
    if (squotes && !dquotes)
        quote = '"';
    if (squotes && quote == '\'') {
        if (newsize > PY_SSIZE_T_MAX - squotes)
            goto overflow;
        newsize += squotes;
    }

    v = PyUnicode_New(newsize, 127);
    if (v == NULL) {
        return NULL;
    }
    p = PyUnicode_1BYTE_DATA(v);
    *p++ = 'b', *p++ = quote;
    for (i = self->index; i < self->length; i++) {
        char c = self->base[i];
        if (c == quote || c == '\\')
            *p++ = '\\', *p++ = c;
        else if (c == '\t')
            *p++ = '\\', *p++ = 't';
        else if (c == '\n')
            *p++ = '\\', *p++ = 'n';
        else if (c == '\r')
            *p++ = '\\', *p++ = 'r';
        else if (c < 0x20 || c > 0x7e) {
            *p++ = '\\';
            *p++ = 'x';
            *p++ = (char) Py_hexdigits[c >> 4];
            *p++ = (char) Py_hexdigits[c & 0xf];
        } else
            *p++ = c;
    }
    *p++ = quote;
    assert(_PyUnicode_CheckConsistency(v, 1));
    return v;
    overflow:
    PyErr_SetString(PyExc_OverflowError,
                    "bytes object is too large to make repr");
    return NULL;
}

PyObject *
IBytes_repr(IBytesObject *self) {
    return to_bytes(self);
}

static PyObject *
IBytes_str(IBytesObject *self) {
    if (Py_BytesWarningFlag) {
        if (PyErr_WarnEx(PyExc_BytesWarning,
                         "str() on a ibytes instance", 1))
            return NULL;
    }
    return to_bytes(self);
}


PyObject *
IBytes_richcompare(IBytesObject *self, PyObject *b, int op) {
    Py_buffer tar;
    if (PyObject_GetBuffer(b, &tar, PyBUF_SIMPLE) == -1) {
        PyErr_Format(PyExc_TypeError, "Can not be compared of 'IBytes' and %.100s", Py_TYPE(b)->tp_name);
        goto error;
    }
    switch (op) {
        case Py_EQ:
            if (IBytes_SIZE(self) != tar.len) goto false;
            if (memcmp(self->base + self->index, tar.buf, tar.len) == 0) goto true;
            goto false;
        case Py_NE:
            if (IBytes_SIZE(self) != tar.len) goto true;
            if (memcmp(self->base + self->index, tar.buf, tar.len) != 0) goto true;
            goto false;
        default:
            PyErr_SetString(PyExc_TypeError, "Only support the same judgment");
            goto error;
    }
    true:
    if (tar.len != -1)
        PyBuffer_Release(&tar);
    Py_RETURN_TRUE;
    false:
    if (tar.len != -1)
        PyBuffer_Release(&tar);
    Py_RETURN_FALSE;
    error:
    if (tar.len != -1)
        PyBuffer_Release(&tar);
    return NULL;
}


static int
IBytes_init(IBytesObject *self, PyObject *args, PyObject *kwds) {
    char *buffer;
    Py_ssize_t buffer_len = 0;
    if (!PyArg_ParseTuple(args, "y#", &buffer, &buffer_len)) {
        return -1;
    };
    if (buffer_len > PY_SSIZE_T_MAX) {
        PyErr_SetString(PyExc_OverflowError, "byte string is too long");
        return -1;
    }
    self->base = PyObject_Malloc(buffer_len);
    if (self->base == NULL) {
        return -1;
    }
    memcpy(self->base, buffer, (size_t) buffer_len);
    self->length = buffer_len;
    self->index = 0;
    return 0;
}

static PyObject *
IBytes_uint8(IBytesObject *self) {
    if (self->index + 1 > self->length) {
        PyErr_SetString(PyExc_IndexError, "index out of range");
        return NULL;
    }
    size_t num = self->base[self->index++];
    PyObject *p = PyLong_FromSize_t(num);
    return p;
}

static PyObject *
IBytes_uint16(IBytesObject *self) {
    if (self->index + 2 > self->length) {
        PyErr_SetString(PyExc_IndexError, "index out of range");
        return NULL;
    }
    size_t num = ((unsigned long) self->base[self->index++]) << 8;
    num += self->base[self->index++];
    PyObject *p = PyLong_FromUnsignedLong(num);
    return p;
}

static PyObject *
IBytes_uint24(IBytesObject *self) {
    if (self->index + 3 > self->length) {
        PyErr_SetString(PyExc_IndexError, "index out of range");
        return NULL;
    }
    size_t num = ((unsigned long) self->base[self->index++]) << 16;
    num += ((unsigned long long) self->base[self->index++]) << 8;
    num += self->base[self->index++];
    PyObject *p = PyLong_FromUnsignedLong(num);
    return p;
}

static PyObject *
IBytes_uint32(IBytesObject *self) {
    if (self->index + 4 > self->length) {
        PyErr_SetString(PyExc_IndexError, "index out of range");
        return NULL;
    }
    size_t num = ((unsigned long) self->base[self->index++]) << 24;
    num += ((unsigned long long) self->base[self->index++]) << 16;
    num += ((unsigned long long) self->base[self->index++]) << 8;
    num += self->base[self->index++];
    PyObject *p = PyLong_FromUnsignedLongLong(num);
    return p;
}

static PyObject *
IBytes_uint64(IBytesObject *self) {
    if (self->index + 8 > self->length) {
        PyErr_SetString(PyExc_IndexError, "index out of range");
        return NULL;
    }
    unsigned long long num = ((unsigned long long) self->base[self->index++]) << 56;
    num += ((unsigned long long) self->base[self->index++]) << 48;
    num += ((unsigned long long) self->base[self->index++]) << 40;
    num += ((unsigned long long) self->base[self->index++]) << 32;
    num += ((unsigned long long) self->base[self->index++]) << 24;
    num += ((unsigned long long) self->base[self->index++]) << 16;
    num += ((unsigned long long) self->base[self->index++]) << 8;
    num += self->base[self->index++];
    PyObject *p = PyLong_FromUnsignedLongLong(num);
    return p;
}

static PyObject *
IBytes_set_index(IBytesObject *self, PyObject *args) {
    Py_ssize_t target_index = 0;
    if (!PyArg_ParseTuple(args, "n", &target_index)) {
        return NULL;
    };
    if (0 > target_index || target_index > self->length) {
        PyErr_SetString(PyExc_IndexError, "index out of range");
        return NULL;
    }
    self->index = target_index;
    Py_RETURN_NONE;
}

static PyObject *
IBytes_get_index(IBytesObject *self) {
    PyObject *p = PyLong_FromSize_t(self->index);
    return p;
}

static PyObject *
IBytes_get(IBytesObject *self, PyObject *args) {
    Py_ssize_t next = 0;
    if (!PyArg_ParseTuple(args, "n", &next)) {
        return NULL;
    };
    if (next < 0 || self->index + next > self->length) {
        PyErr_SetString(PyExc_IndexError, "index out of range");
        return NULL;
    }
    char *outp[next];
    memcpy(outp, self->base + self->index, next);
    self->index += next;
    PyObject *out = Py_BuildValue("y#", outp, next);
    return out;
}

static PyObject *
IBytes_get_obj(IBytesObject *self, PyObject *args) {
    Py_ssize_t next = 0;
    IBytesObject *op;
    char *p = self->base;
    if (!PyArg_ParseTuple(args, "n", &next)) {
        return NULL;
    };

    if (next == 0 && (op = ibytes_null) != NULL) {
        Py_INCREF(op);
        return (PyObject *) op;
    } else if (next > IBytes_SIZE(self)) {
        PyErr_SetString(PyExc_IndexError, "index out of range");
        return NULL;
    }
    op = PyObject_New(IBytesObject, &IBytesType);
    if (op == NULL)
        return NULL;
    if (next == 0) {
        op->base = NULL;
    } else {
        op->base = PyObject_Malloc(next);
        if (op->base == NULL) {
            Py_DECREF(op);
            return PyErr_NoMemory();
        }
        if (p + self->index != NULL && next > 0)
            memcpy(op->base, p + self->index, next);
    }
    op->index = 0;
    op->length = next;
    self->index += next;
    if (next == 0) {
        ibytes_null = op;
        Py_INCREF(op);
    }
    return (PyObject *) op;
}

static PyObject *
IBytes_get_old(IBytesObject *self) {
    char *outp[self->index];
    memcpy(outp, self->base, self->index);
    PyObject *out = Py_BuildValue("y#", outp, self->index);
    return out;
}

static PyObject *
IBytes_get_tuple(IBytesObject *self, PyObject *args) {
    Py_ssize_t next = 0, i;
    if (!PyArg_ParseTuple(args, "n", &next)) {
        return NULL;
    };
    if (next > IBytes_SIZE(self)) {
        PyErr_SetString(PyExc_IndexError, "index out of range");
        return NULL;
    }
    PyObject *op = PyTuple_New(next);
    if (op == NULL) {
        return PyErr_NoMemory();
    }
    for (i = 0; i < next; i++) {
        if (PyTuple_SetItem(op, i, PyLong_FromUnsignedLong((unsigned long) self->base[self->index++]))) {
            Py_DECREF(op);
            PyErr_SetString(PyExc_ValueError, "Unable to set tuple value");
            return NULL;
        }
    }
    return op;
}

static PyObject *
IBytes_vectors(IBytesObject *self, PyObject *args) {
    Py_ssize_t length = 0, i = 0, true_length = 0;
    if (!PyArg_ParseTuple(args, "n", &length)) {
        return NULL;
    };
    if (length > 8) {
        PyErr_SetString(PyExc_ValueError, "char too long");
        return NULL;
    }
    for (i = 0; i < length; i++) {
        true_length += ((unsigned long long) self->base[self->index++]) << (i * 8);
    }
    if (true_length > (IBytes_SIZE(self) - length)) {
        PyErr_SetString(PyExc_IndexError, "index out of range");
        return NULL;
    }

    IBytesObject *op = PyObject_New(IBytesObject, &IBytesType);
    op->base = PyObject_Malloc(true_length);
    if (op->base == NULL) {
        Py_DECREF(op);
        return PyErr_NoMemory();
    }
    memcpy(op->base, self->base + self->index, true_length);
    op->length = true_length;
    op->index = 0;
    self->index += true_length;
    return (PyObject *) op;
}


static PyObject *
IBytes_variable(IBytesObject *self) {
    Py_ssize_t flag, head = 0, num = 0;
    flag = ((unsigned long) self->base[self->index]) >> 6;\
    head = flag << (((Py_ssize_t) pow(2, flag) * 8) - 2);
    if (self->index + (Py_ssize_t) pow(2, flag) > self->length) {
        PyErr_SetString(PyExc_IndexError, "index out of range");
        return NULL;
    }
    switch (flag) {
        case 3:
            num += ((unsigned long long) self->base[self->index++]) << 56;
            num += ((unsigned long long) self->base[self->index++]) << 48;
            num += ((unsigned long long) self->base[self->index++]) << 40;
            num += ((unsigned long long) self->base[self->index++]) << 32;
        case 2:
            num += ((unsigned long long) self->base[self->index++]) << 24;
            num += ((unsigned long long) self->base[self->index++]) << 16;
        case 1:
            num += ((unsigned long long) self->base[self->index++]) << 8;
        case 0:
            num += self->base[self->index++];
            break;
        default:
            break;
    }
    return PyLong_FromSsize_t(num - head);
}

static PyObject *
IBytes_find(IBytesObject *self, PyObject *args, PyObject *keywds) {
    int start = 0, step = 0, length = 0;
    char *key;
    static char *kwlist[] = {"key", "start", "end", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, keywds, "y#|nn", kwlist, &key, &length, &start, &step))
        return NULL;
    if (step == NULL || step == 0) {
        step = self->length - self->index;
    } else if (step > IBytes_SIZE(self) || step < (self->index - self->length)) {
        PyErr_SetString(PyExc_IndexError, "index out of range");
        return NULL;
    } else if (step < 0) {
        step = self->length - self->index + step;
    }
    if (start == NULL || start == 0) {
        start = self->index;
    } else if (start > IBytes_SIZE(self) || start < (self->index - self->length)) {
        PyErr_SetString(PyExc_IndexError, "index out of range");
        return NULL;
    } else if (start < 0) {
        start = self->length + start;
    } else {
        start = self->index + start;
    }
    if ((start + step) > self->length) {
        return PyLong_FromLong(-1);
    }
    int index = bytes_search(key, length, self->base + start, step);
    return PyLong_FromLong(index);
}

static PyObject *
IBytes_raw_find(IBytesObject *self, PyObject *args, PyObject *keywds) {
    Py_ssize_t start = NULL, step = NULL, length;
    char *key;
    static char *kwlist[] = {"key", "start", "end", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, keywds, "y#|nn", kwlist, &key, &length, &start, &step))
        return NULL;
    if (step == NULL || step == 0) {
        step = self->length;
    } else if (step > self->length) {
        PyErr_SetString(PyExc_IndexError, "index out of range");
        return NULL;
    } else if (step < 0) {
        step = self->length + step;
    }
    if (start == NULL || start == 0) {
        start = 0;
    } else if (start > self->length) {
        PyErr_SetString(PyExc_IndexError, "index out of range");
        return NULL;
    } else if (start < 0) {
        start = self->length + start;
    }
    if (start < step) {
        return PyLong_FromLong(-1);
    }
    int index = bytes_search(key, length, self->base + start, step);
    if (index == -1) {
        return PyLong_FromLong(-1);
    }
    return PyLong_FromLong(index + start);
}

static PyObject *
IBytes_compress_pointer(IBytesObject *self) {
    if (self->index + 1 > self->length) {
        PyErr_SetString(PyExc_IndexError, "index out of range");
        return NULL;
    }
    if (self->base[self->index] >> 6 != 3) {
        Py_RETURN_NONE;
    }
    int index = ((self->base[self->index++] - 192) << 8) + self->base[self->index++];
    return PyLong_FromLong(index);
}


static PyObject *
IBytes_next(IBytesObject *self, PyObject *args) {
    Py_ssize_t next = 0;
    if (!PyArg_ParseTuple(args, "n", &next)) {
        return NULL;
    };
    if (self->index > self->length - next) {
        PyErr_SetString(PyExc_IndexError, "index out of range");
        return NULL;
    }
    self->index += next;
    Py_RETURN_NONE;
}


static PyMethodDef IBytes_methods[] = {
        {"uint8",     (PyCFunction) IBytes_uint8,            METH_NOARGS,  "Get byte stream network order 8bit"},
        {"uint16",    (PyCFunction) IBytes_uint16,           METH_NOARGS,  "Get byte stream network order 16bit"},
        {"uint24",    (PyCFunction) IBytes_uint24,           METH_NOARGS,  "Get byte stream network order 24bit"},
        {"uint32",    (PyCFunction) IBytes_uint32,           METH_NOARGS,  "Get byte stream network order 32bit"},
        {"uint64",    (PyCFunction) IBytes_uint64,           METH_NOARGS,  "Get byte stream network order 64bit"},
        {"variable",  (PyCFunction) IBytes_variable,         METH_NOARGS,  "TLS 13 variable"},
        {"get_index", (PyCFunction) IBytes_get_index,        METH_NOARGS,  "Get Index"},
        {"set_index", (PyCFunction) IBytes_set_index,        METH_VARARGS, "Set Index"},
        {"get",       (PyCFunction) IBytes_get,              METH_VARARGS, "Get bytes"},
        {"get_obj",   (PyCFunction) IBytes_get_obj,          METH_VARARGS, "Get IBytes"},
        {"get_tuple", (PyCFunction) IBytes_get_tuple,        METH_VARARGS, "Get tuple"},
        {"get_old",   (PyCFunction) IBytes_get_old,          METH_NOARGS,  "Get old IBytes"},
        {"find",      (PyCFunction) IBytes_find,             METH_VARARGS | METH_KEYWORDS, "find"},
        {"raw_find",  (PyCFunction) IBytes_raw_find,         METH_VARARGS | METH_KEYWORDS, "raw bytes find"},
        {"vectors",   (PyCFunction) IBytes_vectors,          METH_VARARGS, "raw bytes vectors"},
        {"next",      (PyCFunction) IBytes_next,             METH_VARARGS, "goto index + n"},
        {"cpointer",  (PyCFunction) IBytes_compress_pointer, METH_NOARGS,  "dns compress pointer"},
        {NULL}  /* Sentinel */
};


static Py_ssize_t
ibytes_length(IBytesObject *a) {
    return a->length - a->index;
}

static PyObject *
ibytes_concat(IBytesObject *a, PyObject *b) {
    char *Bx = NULL, *By = NULL;
    PyObject *result = NULL;
    Py_ssize_t Lx = 0, Ly = 0;
    Bx = a->base + a->index;
    Lx = a->length - a->index;
    if (PyBytes_Check(b)) {
        PyBytes_AsStringAndSize(b, &By, &Ly);
    } else if (IBytes_Check(b)) {
        IBytesObject *Ib = (IBytesObject *) b;
        By = Ib->base + Ib->index;
        Ly = Ib->length - Ib->index;
    } else if (PyByteArray_Check(b)) {
        By = (char *) PyByteArray_AS_STRING(b);
        Ly = PyByteArray_GET_SIZE(b);
    } else {
        PyErr_Format(PyExc_TypeError, "can't concat IBytes to %.100s", Py_TYPE(b)->tp_name);
        goto done;
    }
    if (Lx == 0 && PyBytes_CheckExact(b)) {
        result = b;
        Py_INCREF(result);
        goto done;
    }
    if (Lx > PY_SSIZE_T_MAX - Ly) {
        PyErr_NoMemory();
        goto done;
    }
    IBytesObject *op = PyObject_New(IBytesObject, &IBytesType);
    op->base = PyObject_Malloc(Lx + Ly);
    if (op->base == NULL) {
        Py_DECREF(op);
        PyErr_NoMemory();
        goto done;
    }
    memcpy(op->base, Bx, Lx);
    memcpy(op->base + Lx, By, Ly);
    op->index = 0;
    op->length = Lx + Ly;
    return (PyObject *) op;
    done:
    return result;
}


static PyObject *
ibytes_repeat(IBytesObject *a, Py_ssize_t count) {
    Py_ssize_t fragment_length = a->length - a->index, new_length;
    if (count > PY_SSIZE_T_MAX / fragment_length) {
        return PyErr_NoMemory();
    }
    new_length = fragment_length * count;
    IBytesObject *op = PyObject_New(IBytesObject, &IBytesType);
    op->base = PyObject_Malloc(new_length);
    if (op->base == NULL) {
        Py_DECREF(op);
        return PyErr_NoMemory();
    }
    char *f = a->base + a->index;
    Py_ssize_t i;
    for (i = 0; i < new_length; i += fragment_length)
        memcpy(op->base + i, f, fragment_length);
    op->index = 0;
    op->length = new_length;
    return (PyObject *) op;
}


static PyObject *
ibytes_inplace_concat(IBytesObject *self, PyObject *b) {
    Py_buffer substring;
    if (PyObject_GetBuffer(b, &substring, PyBUF_SIMPLE) == -1) {
        PyErr_Format(PyExc_TypeError, "can't concat IBytes to %.100s", Py_TYPE(b)->tp_name);
        goto error;
    }
    if (self->length > PY_SSIZE_T_MAX - substring.len) {
        PyErr_NoMemory();
        goto error;
    }
    void *p = PyObject_Realloc(self->base, self->length + substring.len);
    if (!p) {
        PyErr_SetString(PyExc_MemoryError, "Unable to allocate memory");
        goto error;
    }
    self->base = p;
    memcpy(self->base + self->length, substring.buf, substring.len);
    self->length += substring.len;
    PyBuffer_Release(&substring);
    Py_INCREF(self);
    return (PyObject *) self;
    error:
    if (substring.len != -1)
        PyBuffer_Release(&substring);
    return NULL;
}


static PyObject *
ibytes_inplace_repeat(IBytesObject *a, Py_ssize_t count) {
    Py_ssize_t fragment_length = a->length - a->index, new_length;

    if (count > PY_SSIZE_T_MAX / fragment_length) {
        return PyErr_NoMemory();
    }
    new_length = fragment_length * count + a->index;

    void *p = PyObject_Realloc(a->base, new_length);
    if (p == NULL) {
        return PyErr_NoMemory();
    }
    a->base = p;
    char *f = a->base + a->index;
    Py_ssize_t i;
    for (i = a->index; i < new_length; i += fragment_length) {
        memcpy(a->base + i, f, fragment_length);
    }
    a->length = new_length;
    Py_INCREF(a);
    return (PyObject *) a;
}


static PyObject *
ibytes_item(IBytesObject *a, Py_ssize_t i) {
    if ((i + a->index) >= a->length) {
        PyErr_SetString(PyExc_IndexError, "index out of range");
        return NULL;
    }
    Py_ssize_t k = (Py_ssize_t) a->base[a->index + i];
    return PyLong_FromSsize_t(k);
}

static int
ibytes_ass_item(IBytesObject *a, Py_ssize_t i, PyObject *v) {
    if ((i + a->index) >= a->length) {
        PyErr_SetString(PyExc_IndexError, "index out of range");
        return -1;
    }
    if (v == NULL) {
        PyErr_SetString(PyExc_TypeError, "'IBytes' object doesn't support item deletion");
        return -1;
    }

    if (!PyLong_Check(v)) goto error;
    Py_ssize_t k = PyLong_AsSsize_t(v);
    if (0 > k || k > 255) goto error;
    a->base[a->index + i] = (char) k;
    return 0;
    error:
    PyErr_SetString(PyExc_TypeError, "an integer is required");
    return -1;
}


static int
ibytes_contains(IBytesObject *a, PyObject *v) {
    Py_ssize_t ival = PyNumber_AsSsize_t(v, NULL);
    if (ival == -1 && PyErr_Occurred()) {
        Py_buffer varg;
        Py_ssize_t pos;
        PyErr_Clear();
        if (PyObject_GetBuffer(v, &varg, PyBUF_SIMPLE) != 0)
            return -1;
        pos = bytes_search(varg.buf, varg.len, a->base + a->index, a->length - a->index);
        PyBuffer_Release(&varg);
        return pos >= 0;
    }
    if (ival < 0 || ival >= 256) {
        PyErr_SetString(PyExc_ValueError, "byte must be in range(0, 256)");
        return -1;
    }
    return memchr(a->base + a->index, (int) ival, a->length - a->index) != NULL;
}

static PyObject *
ibytes_subscript(IBytesObject *self, PyObject *item) {
    if (PyIndex_Check(item)) {
        Py_ssize_t i = PyNumber_AsSsize_t(item, PyExc_IndexError);
        if (i == -1 && PyErr_Occurred())
            return NULL;
        if (i < 0) {
            if (i + self->length < self->index) {
                PyErr_SetString(PyExc_IndexError, "index out of range");
                return NULL;
            }
            i += PyBytes_GET_SIZE(self);
        } else if (i >= 0 && i < IBytes_SIZE(self)) {
            i += self->index;
        } else {
            PyErr_SetString(PyExc_IndexError, "index out of range");
            return NULL;
        }
        return PyLong_FromLong(self->base[i]);
    } else if (PySlice_Check(item)) {
        Py_ssize_t start, stop, step, slicelength, cur, i;
        char *source_buf;
        char *result_buf;
        PyObject *result;

        if (PySlice_Unpack(item, &start, &stop, &step) < 0) {
            return NULL;
        }
        slicelength = PySlice_AdjustIndices(IBytes_SIZE(self), &start, &stop, step);

        if (slicelength <= 0) {
            return PyBytes_FromStringAndSize("", 0);
        } else if (start == 0 && step == 1 && slicelength == IBytes_SIZE(self)) {
            PyObject *result = PyBytes_FromStringAndSize(NULL, IBytes_SIZE(self));
            if (result != NULL) {
                memcpy(PyBytes_AS_STRING(result), IBytes_Data(self), IBytes_SIZE(self));
            }
            return result;
        } else if (step == 1) {
            return PyBytes_FromStringAndSize(IBytes_Data(self) + start, slicelength);
        } else {

            source_buf = IBytes_Data(self);
            result = PyBytes_FromStringAndSize(NULL, slicelength);
            if (result == NULL)
                return NULL;

            result_buf = PyBytes_AS_STRING(result);
            for (cur = start, i = 0; i < slicelength;
                 cur += step, i++) {
                result_buf[i] = source_buf[cur];
            }
            return result;
        }
    } else {
        PyErr_Format(PyExc_TypeError, "byte indices must be integers or slices, not %.200s", Py_TYPE(item)->tp_name);
        return NULL;
    }
}


static PySequenceMethods ibytes_as_sequence = {
        (lenfunc) ibytes_length,                  /*sq_length*/
        (binaryfunc) ibytes_concat,                  /*sq_concat*/
        (ssizeargfunc) ibytes_repeat,                  /*sq_repeat*/
        (ssizeargfunc) ibytes_item,                  /*sq_item*/
        0,
        (ssizeobjargproc) ibytes_ass_item,                  /*sq_ass_item*/
        0,
        (objobjproc) ibytes_contains,                  /*sq_contains*/
        (binaryfunc) ibytes_inplace_concat,                  /*sq_inplace_concat*/
        (ssizeargfunc) ibytes_inplace_repeat,                  /*sq_inplace_repeat*/
};


static int
ibytes_getbuffer(IBytesObject *obj, Py_buffer *view, int flags) {
    if (view == NULL) {
        PyErr_SetString(PyExc_BufferError,
                        "bytearray_getbuffer: view==NULL argument is obsolete");
        return -1;
    }
    (void) PyBuffer_FillInfo(view, (PyObject *) obj, obj->base + obj->index, obj->length - obj->index, 0, flags);
    return 0;
}

static void
ibytes_releasebuffer(PyByteArrayObject *obj, Py_buffer *view) {
}


static PyBufferProcs ibytes_as_bufferprocs = {
        (getbufferproc) ibytes_getbuffer,
        (releasebufferproc) ibytes_releasebuffer
};


static PyMappingMethods ibytes_as_mapping = {
        0,
        (binaryfunc) ibytes_subscript,
        0,
};

PyTypeObject IBytesType = {
        PyVarObject_HEAD_INIT(NULL, 0)
        "ibytes",             /* tp_name */
        sizeof(IBytesObject), /* tp_basicsize */
        0,                         /* tp_itemsize */
        (destructor) IBytes_dealloc, /* tp_dealloc */
        0,                         /* tp_print */
        0,                         /* tp_getattr */
        0,                         /* tp_setattr */
        0,                         /* tp_reserved */
        (reprfunc) IBytes_repr,                         /* tp_repr */
        0,                         /* tp_as_number */
        &ibytes_as_sequence,                         /* tp_as_sequence */
        &ibytes_as_mapping,                         /* tp_as_mapping */
        0,                         /* tp_hash  */
        0,                         /* tp_call */
        (reprfunc) IBytes_str,                         /* tp_str */
        0,                         /* tp_getattro */
        0,                         /* tp_setattro */
        &ibytes_as_bufferprocs,                         /* tp_as_buffer */
        Py_TPFLAGS_BASETYPE,   /* tp_flags */
        "IBytes objects Network stream optimization",           /* tp_doc */
        0,                         /* tp_traverse */
        0,                         /* tp_clear */
        (richcmpfunc) IBytes_richcompare,                         /* tp_richcompare */
        0,                         /* tp_weaklistoffset */
        0,                         /* tp_iter */
        0,                         /* tp_iternext */
        IBytes_methods,             /* tp_methods */
        0,             /* tp_members */
        0,                         /* tp_getset */
        0,                         /* tp_base */
        0,                         /* tp_dict */
        0,                         /* tp_descr_get */
        0,                         /* tp_descr_set */
        0,                         /* tp_dictoffset */
        (initproc) IBytes_init,      /* tp_init */
        0,                         /* tp_alloc */
        PyType_GenericNew,                 /* tp_new */
};


static PyModuleDef IBytesmodule = {
        PyModuleDef_HEAD_INIT,
        "ibytes",
        "Example module that creates an extension type.",
        -1,
        NULL, NULL, NULL, NULL, NULL
};

PyMODINIT_FUNC
PyInit_ibytes(void) {
    PyObject *m;
    if (PyType_Ready(&IBytesType) < 0)
        return NULL;

    m = PyModule_Create(&IBytesmodule);
    if (m == NULL)
        return NULL;
    Py_INCREF(&IBytesType);
    PyModule_AddObject(m, "ibytes", (PyObject *) &IBytesType);
    return m;
}
