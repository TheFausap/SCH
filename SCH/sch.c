/*
 * Bootstrap Scheme - a quick and very dirty Scheme interpreter.
 * Copyright (C) 2010 Peter Michaux (http://peter.michaux.ca/)
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Affero General Public
 * License version 3 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License version 3 for more details.
 *
 * You should have received a copy of the GNU Affero General Public
 * License version 3 along with this program. If not, see
 * <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <complex.h>

#define BUFFER_MAX 1000            /* max string length */
#define STACK_MAX 2048             /* VM Size */
#define INITIAL_GC_THRESHOLD 1000  /* maximum number of obj to start GC */

#define add_procedure(scheme_name, c_name)    \
    define_var(make_symbol(scheme_name),      \
                    make_primitive(c_name),   \
                    env);

/**************************** MODEL ******************************/

typedef enum {
    BOOLEAN, FIXNUM, CHARACTER, FLONUM,
    CPXNUM, STRING, PAIR, THE_NIL, SYMBOL,
    PRIMITIVE_PROC, COMPOUND_PROC, INPUT_PORT,
    OUTPUT_PORT, EOF_OBJECT
} object_type;

#if defined(_MSC_VER)
typedef _Dcomplex sComplex;
#else
typedef double complex sComplex;
#endif

typedef struct object {
    object_type type;
    unsigned char marked;
    struct object* next;
    union {
        struct {
            char value;
        } boolean;
        struct {
            char* value;
        } symbol;
        struct {
            long value;
        } fixnum;
        struct {
            double value;
        } flonum;
        struct {
            sComplex value;
        } cpxnum;
        struct {
            char value;
        } character;
        struct {
            char* value;
        } string;
        struct {
            struct object* car;
            struct object* cdr;
        } pair;
        struct {
            struct object* (*fn)(struct object* args);
        } primitive_proc;
        struct {
            struct object* params;
            struct object* body;
            struct object* env;
            /* TODO: keys, and optional like COMMON LISP */
        } compound_proc;
        struct {
            FILE* stream;
        } input_port;
        struct {
            FILE* stream;
        } output_port;
    } data;
} object;

typedef struct {
    int numObj;
    int maxObj;
    object* firstObject;
    object* stack[STACK_MAX];
    int stackSize;
} VM;

VM* the_vm;

object * false;
object * true;
object* nil;
object* symtab;

VM* newVM(void) {
    VM* vm = malloc(sizeof(VM));

    if (vm) {
        vm->stackSize = 0;
        vm->firstObject = NULL;
        vm->numObj = 0;
        vm->maxObj = INITIAL_GC_THRESHOLD;
    }
    else {
        fprintf(stderr, "*** cannot allocate VM for the stack\n");
    }
    return vm;
}

void push(VM* vm, object* val) {
    if (vm->stackSize >= STACK_MAX) {
        fprintf(stderr, "*** STACK OVERFLOW\n");
        exit(1);
    }
    else {
        vm->stack[vm->stackSize++] = val;
    }
}

object* pop(VM* vm) {
    if (vm->stackSize <= 0) {
        fprintf(stderr, "*** STACK UNDERFLOW\n");
        exit(1);
    }
    else {
        return vm->stack[--vm->stackSize];
    }
}

void mark(object* obj) {

    if (obj->marked) return;

    obj->marked = 1;

    if (obj->type == PAIR) {
        mark(obj->data.pair.car);
        mark(obj->data.pair.cdr);
    }
    else if (obj->type == COMPOUND_PROC) {
        mark(obj->data.compound_proc.body);
        mark(obj->data.compound_proc.env);
        mark(obj->data.compound_proc.params);
    }
}

void markAll(VM* vm) {
    for (int i = 0; i < vm->stackSize; i++) {
        mark(vm->stack[i]);
    }
}

void sweep(VM* vm) {
    object** obj = &vm->firstObject;
    while (*obj) {
        if (!(*obj)->marked) {
            /* This obj wasn't reached so remove it from the list */
            object* unreached = *obj;
            *obj = unreached->next;
            free(unreached);

            vm->numObj--;
        }
        else {
            /* This obj was reached, so unmark it (for the next GC) */
            (*obj)->marked = 0;
            obj = &(*obj)->next;
        }
    }
}

void gc(VM* vm) {
    int numObj = vm->numObj;

    printf("*** GC: marking %d objects\n", numObj);
    markAll(vm);
    printf("*** GC: sweeping\n");
    sweep(vm);

    vm->maxObj = vm->numObj == 0 ? INITIAL_GC_THRESHOLD : vm->numObj * 2;

    printf("*** GC: collected %d objects, %d remaining.\n", numObj - vm->numObj,
        vm->numObj);
}

object* gc_proc(object* dummy) {
    gc(the_vm);
    return nil;
}

object* gc_stats_proc(object* dummy) {
    printf("*** GARBAGE COLLECTOR STATS ***\n");
    printf("*** Current number of objs: %d\n", the_vm->numObj);
    printf("*** Maximum number of objs: %d\n", the_vm->maxObj);
    return nil;
}

void freeVM(VM* vm) {
    vm->stackSize = 0;
    gc(vm);
    free(vm);
}

object* alloc_object(void) {
    object* obj;

    if (the_vm->numObj == the_vm->maxObj) gc(the_vm);

    obj = malloc(sizeof(object));
    if (obj == NULL) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    obj->marked = 0;
    obj->next = the_vm->firstObject;
    the_vm->numObj++;
    the_vm->firstObject = obj;

    return obj;
}



/**************** SYMBOL DEFINITION ***********/

object* quote_symbol;
object* set_symbol;
object* define_symbol;
object* ok_symbol;
object* if_symbol;
object* lambda_symbol;
object* begin_symbol;
object* cond_symbol;
object* else_symbol;
object* let_symbol;
object* and_symbol;
object* or_symbol;

object* eof_object;
object* the_empty;
object* the_global;


object* cons(object* car, object* cdr); /* forward declaration */
object* car(object* pair);
object* cdr(object* pair);


char is_nil(object* obj) {
    return obj == nil;
}

char is_boolean(object* obj) {
    return obj->type == BOOLEAN;
}

char is_false(object* obj) {
    return obj == false;
}

char is_true(object* obj) {
    return !is_false(obj);
}

object* make_symbol(char* value) {
    object* obj;
    object* elem;

    /* search in table O(n) */
    elem = symtab;
    while (!is_nil(elem)) {
        if (strcmp(car(elem)->data.symbol.value, value) == 0) {
            return car(elem);
        }
        elem = cdr(elem);
    };

    /* nothing found. new symb to add */
    obj = alloc_object();
    obj->type = SYMBOL;
    obj->data.symbol.value = malloc(strlen(value) + 1);
    if (obj->data.symbol.value == NULL) {
        fprintf(stderr, "*** symbol - out of memory\n");
        exit(1);
    }
    strcpy(obj->data.symbol.value, value);
    symtab = cons(obj, symtab);

    push(the_vm, obj);
    return obj;
}

char is_symbol(object* obj) {
    return obj->type == SYMBOL;
}

object* make_fixnum(long value) {
    object* obj;

    obj = alloc_object();
    obj->type = FIXNUM;
    obj->data.fixnum.value = value;
    push(the_vm, obj);
    return obj;
}

char is_fixnum(object* obj) {
    return obj->type == FIXNUM;
}

object* make_flonum(double value) {
    object* obj;

    obj = alloc_object();
    obj->type = FLONUM;
    obj->data.flonum.value = value;
    push(the_vm, obj);
    return obj;
}

char is_flonum(object* obj) {
    return obj->type == FLONUM;
}

object* make_cpxnum(double re, double im) {
    object* obj;

    obj = alloc_object();
    obj->type = CPXNUM;
#if defined(_MSC_VER)
    obj->data.cpxnum.value = _Cbuild(re, im);
#else
    obj->data.cpxnum.value = complex(re, im);
#endif
    push(the_vm, obj);
    return obj;
}

object* make_cpxnum2(sComplex z) {
    object* obj;

    obj = alloc_object();
    obj->type = CPXNUM;
    obj->data.cpxnum.value = z;
    push(the_vm, obj);
    return obj;
}

char is_cpxnum(object* obj) {
    return obj->type == CPXNUM;
}

object* make_character(char value) {
    object* obj;

    obj = alloc_object();
    obj->type = CHARACTER;
    obj->data.character.value = value;
    push(the_vm, obj);
    return obj;
}

char is_character(object* obj) {
    return obj->type == CHARACTER;
}

object* make_string(char* value) {
    object* obj;

    obj = alloc_object();
    obj->type = STRING;
    obj->data.string.value = malloc(strlen(value) + 1);
    if (obj->data.string.value == NULL) {
        fprintf(stderr, "*** cannot create string - out of memory\n");
        exit(1);
    }
    strcpy(obj->data.string.value, value);
    push(the_vm, obj);
    return obj;
}

char is_string(object* obj) {
    return obj->type == STRING;
}

object* cons(object* car, object* cdr) {
    object* obj;

    obj = alloc_object();
    obj->type = PAIR;
    obj->data.pair.car = car;
    obj->data.pair.cdr = cdr;
    push(the_vm, obj);
    return obj;
}

char is_pair(object* obj) {
    return obj->type == PAIR;
}

object* car(object* pair) {
    return pair->data.pair.car;
}

void set_car(object* obj, object* value) {
    obj->data.pair.car = value;
}

object* cdr(object* pair) {
    return pair->data.pair.cdr;
}

void set_cdr(object* obj, object* value) {
    obj->data.pair.cdr = value;
}

#define caar(obj)   car(car(obj))
#define cadr(obj)   car(cdr(obj))
#define cdar(obj)   cdr(car(obj))
#define cddr(obj)   cdr(cdr(obj))
#define caaar(obj)  car(car(car(obj)))
#define caadr(obj)  car(car(cdr(obj)))
#define cadar(obj)  car(cdr(car(obj)))
#define caddr(obj)  car(cdr(cdr(obj)))
#define cdaar(obj)  cdr(car(car(obj)))
#define cdadr(obj)  cdr(car(cdr(obj)))
#define cddar(obj)  cdr(cdr(car(obj)))
#define cdddr(obj)  cdr(cdr(cdr(obj)))
#define caaaar(obj) car(car(car(car(obj))))
#define caaadr(obj) car(car(car(cdr(obj))))
#define caadar(obj) car(car(cdr(car(obj))))
#define caaddr(obj) car(car(cdr(cdr(obj))))
#define cadaar(obj) car(cdr(car(car(obj))))
#define cadadr(obj) car(cdr(car(cdr(obj))))
#define caddar(obj) car(cdr(cdr(car(obj))))
#define cadddr(obj) car(cdr(cdr(cdr(obj))))
#define cdaaar(obj) cdr(car(car(car(obj))))
#define cdaadr(obj) cdr(car(car(cdr(obj))))
#define cdadar(obj) cdr(car(cdr(car(obj))))
#define cdaddr(obj) cdr(car(cdr(cdr(obj))))
#define cddaar(obj) cdr(cdr(car(car(obj))))
#define cddadr(obj) cdr(cdr(car(cdr(obj))))
#define cdddar(obj) cdr(cdr(cdr(car(obj))))
#define cddddr(obj) cdr(cdr(cdr(cdr(obj))))

object* make_primitive(object* (*fn)(struct object* args)) {
    object* obj;

    obj = alloc_object();
    obj->type = PRIMITIVE_PROC;
    obj->data.primitive_proc.fn = fn;
    push(the_vm, obj);
    return obj;
}

char is_primitive(object* obj) {
    return obj->type == PRIMITIVE_PROC;
}

object* is_null_proc(object* arguments) {
    return is_nil(car(arguments)) ? true : false;
}

object* is_boolean_proc(object* arguments) {
    return is_boolean(car(arguments)) ? true : false;
}

object* is_symbol_proc(object* arguments) {
    return is_symbol(car(arguments)) ? true : false;
}

char is_number(object* obj) {
    return ((obj->type == FIXNUM) ||
            (obj->type == FLONUM) ||
            (obj->type == CPXNUM));
}

object* is_integer_proc(object* arguments) {
    return is_fixnum(car(arguments)) ? true : false;
}

object* is_real_proc(object* arguments) {
    return is_flonum(car(arguments)) ? true : false;
}

object* is_complex_proc(object* arguments) {
    return is_number(car(arguments)) ? true : false;
}

object* is_char_proc(object* arguments) {
    return is_character(car(arguments)) ? true : false;
}

object* is_string_proc(object* arguments) {
    return is_string(car(arguments)) ? true : false;
}

object* is_pair_proc(object* arguments) {
    return is_pair(car(arguments)) ? true : false;
}

char is_compound_proc(object* obj); /* forward declaration */

object* is_procedure_proc(object* arguments) {
    object* obj;

    obj = car(arguments);
    return (is_primitive(obj) ||
            is_compound_proc(obj)) ?
              true :
              false;
}

object* char_to_integer_proc(object* arguments) {
    return make_fixnum((car(arguments))->data.character.value);
}

object* integer_to_char_proc(object* arguments) {
    return make_character((char)(car(arguments))->data.fixnum.value);
}

object* number_to_string_proc(object* arguments) {
    char buffer[100];

    sprintf(buffer, "%ld", (car(arguments))->data.fixnum.value);
    return make_string(buffer);
}

object* string_to_number_proc(object* arguments) {
    /* TODO: Adding FLONUM support */
    return make_fixnum(atoi((car(arguments))->data.string.value));
}

object* symbol_to_string_proc(object* arguments) {
    return make_string((car(arguments))->data.symbol.value);
}

object* string_to_symbol_proc(object* arguments) {
    return make_symbol((car(arguments))->data.string.value);
}

object* add_proc(object* args) {
    long result = 0;
    double dresult = 0;
    double re = 0.0;
    double im = 0.0;
    short op_type = 0;

    while (!is_nil(args)) {
        if (is_fixnum(car(args))) {
            result += (car(args))->data.fixnum.value;
        }
        else if (is_flonum(car(args))) {
            dresult += (car(args))->data.flonum.value;
            op_type = 1;
        }
        else if (is_cpxnum(car(args))) {
            re += creal((car(args))->data.cpxnum.value);
            im += cimag((car(args))->data.cpxnum.value);
            op_type = 2;
        }
        args = cdr(args);
    }
    switch (op_type) {
    case 0:
        return make_fixnum(result);
        break;
    case 1:
        dresult += (double)result;
        return make_flonum(dresult);
        break;
    case 2:
        re += (double)result;
        re += dresult;
        return make_cpxnum(re, im);
        break;
    default:
        return nil;
    }
}

object* sub_proc(object* args) {
    long result = 0;
    double dresult = 0;
    double re = 0.0;
    double im = 0.0;
    short op_type = 0;

    if (is_fixnum(car(args))) {
        result = (car(args))->data.fixnum.value;
    }
    else if (is_flonum(car(args))) {
        dresult = (car(args))->data.flonum.value;
        op_type = 1;
    }
    else if (is_cpxnum(car(args))) {
        re = creal((car(args))->data.cpxnum.value);
        im = cimag((car(args))->data.cpxnum.value);
        op_type = 2;
    }
    while (!is_nil(args = cdr(args))) {
        if (is_fixnum(car(args))) {
            result -= (car(args))->data.fixnum.value;
        }
        else if (is_flonum(car(args))) {
            dresult -= (car(args))->data.flonum.value;
            op_type = 1;
        }
        else if (is_cpxnum(car(args))) {
            re -= creal((car(args))->data.cpxnum.value);
            im -= cimag((car(args))->data.cpxnum.value);
            op_type = 2;
        }
    }
    switch (op_type) {
    case 0:
        return make_fixnum(result);
        break;
    case 1:
        dresult += (double)result;
        return make_flonum(dresult);
        break;
    case 2:
        re += (double)result;
        re += dresult;
        return make_cpxnum(re, im);
        break;
    default:
        return nil;
    }
}

object* mul_proc(object* args) {
    long result = 1;
    double dresult = 1;
    double re = 1.0;
    double im = 1.0;
    sComplex cresult;
    short op_type = 0;
    short full_cpx = 1;

    cresult = _Cbuild(1.0, 0.0);

    while (!is_nil(args)) {
        if (is_fixnum(car(args))) {
            result *= (car(args))->data.fixnum.value;
            full_cpx &= 0;
        }
        else if (is_flonum(car(args))) {
            dresult *= (car(args))->data.flonum.value;
            op_type = 1;
            full_cpx &= 0;
        }
        else if (is_cpxnum(car(args))) {
#if defined(_MSC_VER)
            cresult = _Cmulcc(cresult, (car(args))->data.cpxnum.value);
#else
            cresult *= (car(args))->data.cpxnum.value;
#endif
            op_type = 2;
            full_cpx &= 1;
        }
        args = cdr(args);
    }
    switch (op_type) {
    case 0:
        return make_fixnum(result);
        break;
    case 1:
        dresult *= (double)result;
        return make_flonum(dresult);
        break;
    case 2:
        if (full_cpx) {
            return make_cpxnum2(cresult);
        }
        else {
            re *= (double)result;
            re *= dresult;
#if defined(_MSC_VER)
            cresult = _Cmulcr(cresult, re);
#else
            cresult *= re;
#endif
            return make_cpxnum2(cresult);
        }
        break;
    default:
        return nil;
    }
}

object* quotient_proc(object* arguments) {
    return make_fixnum(
        ((car(arguments))->data.fixnum.value) /
        ((cadr(arguments))->data.fixnum.value));
}

object* remainder_proc(object* arguments) {
    return make_fixnum(
        ((car(arguments))->data.fixnum.value) %
        ((cadr(arguments))->data.fixnum.value));
}

sComplex cinv(sComplex z1) {
    return _Cmulcr(conj(z1), 1.0/pow(cabs(z1),2));
}

object* div_proc(object* args) {
    double result = 1.0;
    double dresult = 1.0;
    double re = 1.0;
    double im = 1.0;
    sComplex cresult;
    short op_type = 0;
    short full_cpx = 1;

    cresult = _Cbuild(1.0, 0.0);

    if (is_fixnum(car(args))) {
        result = (double)(car(args))->data.fixnum.value;
        full_cpx &= 0;
    }
    else if (is_flonum(car(args))) {
        dresult = (car(args))->data.flonum.value;
        op_type = 1;
        full_cpx &= 0;
    }
    else if (is_cpxnum(car(args))) {
        cresult = (car(args))->data.cpxnum.value;
        op_type = 2;
        full_cpx &= 1;
    }

    while (!is_nil(args = cdr(args))) {
        if (is_fixnum(car(args))) {
            result /= (double)(car(args))->data.fixnum.value;
            full_cpx &= 0;
        }
        else if (is_flonum(car(args))) {
            dresult /= (car(args))->data.flonum.value;
            op_type = 1;
            full_cpx &= 0;
        }
        else if (is_cpxnum(car(args))) {
            cresult = _Cmulcc(cresult, cinv((car(args))->data.cpxnum.value));
            op_type = 2;
            full_cpx &= 1;
        }
    }

    switch (op_type) {
    case 0:
        return make_fixnum((long)result);
        break;
    case 1:
        dresult *= result;
        return make_flonum(dresult);
        break;
    case 2:
        if (full_cpx) {
            return make_cpxnum2(cresult);
        }
        else {
            re *= result;
            re *= dresult;
#if defined(_MSC_VER)
            cresult = _Cmulcr(cresult, re);
#else
            cresult *= re;
#endif
            return make_cpxnum2(cresult);
        }
        break;
    default:
        return nil;
    }
}

object* is_numbeq_proc(object* arguments) {
    long value = 0;
    double dvalue = 0.0;
    double re = 0.0;
    double im = 0.0;
    object_type type;
    
    type = (car(arguments))->type;

    switch (type) {
    case FIXNUM:
        value = (car(arguments))->data.fixnum.value;
        break;
    case FLONUM:
        dvalue = (car(arguments))->data.flonum.value;
        break;
    default:
        re = creal((car(arguments))->data.cpxnum.value);
        im = cimag((car(arguments))->data.cpxnum.value);
    }
    
    while (!is_nil(arguments = cdr(arguments))) {
        if (type != (car(arguments))->type) {
            /* cannot compare different number types */
            return false;
        }
        switch (type) {
        case FIXNUM:
            if (value != ((car(arguments))->data.fixnum.value)) {
                return false;
            }
            break;
        case FLONUM:
            if (dvalue != ((car(arguments))->data.flonum.value)) {
                return false;
            }
            break;
        default:
            if ((re != creal((car(arguments))->data.cpxnum.value)) &&
                (im != cimag((car(arguments))->data.cpxnum.value))) {
                return false;
            }
        }
    }
    return true;
}

object* is_lessthan_proc(object* arguments) {
    double previous;
    double next;
    object_type type;

    type = (car(arguments))->type;

    switch (type) {
    case FIXNUM:
        previous = (double)(car(arguments))->data.fixnum.value;
        break;
    case FLONUM:
        previous = (car(arguments))->data.flonum.value;
        break;
    default:
        fprintf(stderr, "*** comparison is not defined for this type\n");
        exit(1);
    }
    
    while (!is_nil(arguments = cdr(arguments))) {
        type = (car(arguments))->type;

        switch (type) {
        case FIXNUM:
            next = (double)(car(arguments))->data.fixnum.value;
            break;
        case FLONUM:
            next = (car(arguments))->data.fixnum.value;
            break;
        default:
            fprintf(stderr, "*** comparison is not defined for this type\n");
            exit(1);
        }
        
        if (previous < next) {
            previous = next;
        }
        else {
            return false;
        }
    }
    return true;
}

object* is_greatthan_proc(object* arguments) {
    double previous;
    double next;
    object_type type;

    type = (car(arguments))->type;

    switch (type) {
    case FIXNUM:
        previous = (double)(car(arguments))->data.fixnum.value;
        break;
    case FLONUM:
        previous = (car(arguments))->data.flonum.value;
        break;
    default:
        fprintf(stderr, "*** comparison is not defined for this type\n");
        exit(1);
    }

    while (!is_nil(arguments = cdr(arguments))) {
        type = (car(arguments))->type;

        switch (type) {
        case FIXNUM:
            next = (double)(car(arguments))->data.fixnum.value;
            break;
        case FLONUM:
            next = (car(arguments))->data.fixnum.value;
            break;
        default:
            fprintf(stderr, "*** comparison is not defined for this type\n");
            exit(1);
        }

        if (previous > next) {
            previous = next;
        }
        else {
            return false;
        }
    }
    return true;
}

object* cons_proc(object* arguments) {
    return cons(car(arguments), cadr(arguments));
}

object* car_proc(object* arguments) {
    return caar(arguments);
}

object* cdr_proc(object* arguments) {
    return cdar(arguments);
}

object* set_car_proc(object* arguments) {
    set_car(car(arguments), cadr(arguments));
    return ok_symbol;
}

object* set_cdr_proc(object* arguments) {
    set_cdr(car(arguments), cadr(arguments));
    return ok_symbol;
}

object* list_proc(object* arguments) {
    return arguments;
}

object* is_eq_proc(object* arguments) {
    object* obj1;
    object* obj2;

    obj1 = car(arguments);
    obj2 = cadr(arguments);

    if (obj1->type != obj2->type) {
        return false;
    }
    switch (obj1->type) {
    case FIXNUM:
        return (obj1->data.fixnum.value ==
            obj2->data.fixnum.value) ?
            true : false;
        break;
    case FLONUM:
        return (obj1->data.flonum.value ==
            obj2->data.flonum.value) ?
            true : false;
        break;
    case CPXNUM:
        return ((creal(obj1->data.cpxnum.value) == 
                 creal(obj2->data.cpxnum.value))   &&
                (cimag(obj1->data.cpxnum.value) ==
                 cimag(obj2->data.cpxnum.value)))
            ? true : false;
        break;
    case CHARACTER:
        return (obj1->data.character.value ==
            obj2->data.character.value) ?
            true : false;
        break;
    case STRING:
        return (strcmp(obj1->data.string.value,
            obj2->data.string.value) == 0) ?
            true : false;
        break;
    default:
        return (obj1 == obj2) ? true : false;
    }
}

object* apply_proc(object* arguments) {
    fprintf(stderr, "*** illegal state: The body of the apply "
        "primitive procedure should not execute.\n");
    exit(1);
}

object* interaction_environment_proc(object* arguments) {
    return the_global;
}

object* setup_env(void);

object* null_environment_proc(object* arguments) {
    return setup_env();
}

object* make_environment(void);

object* environment_proc(object* arguments) {
    return make_environment();
}

object* eval_proc(object* arguments) {
    fprintf(stderr, "*** illegal state: The body of the eval "
        "primitive procedure should not execute.\n");
    exit(1);
}

object* sread(FILE* in);

object* eval(object* exp, object* env);

object* load_proc(object* arguments) {
    char* filename;
    FILE* in;
    object* exp;
    object* result;

    result = alloc_object();
    filename = car(arguments)->data.string.value;
    in = fopen(filename, "r");
    if (in == NULL) {
        fprintf(stderr, "*** could not load file \"%s\"", filename);
        exit(1);
    }
    while ((exp = sread(in)) != NULL) {
        result = eval(exp, the_global);
    }
    fclose(in);
    printf("program-loaded\n");
    return result;
}

object* make_input_port(FILE* in);

object* open_input_port_proc(object* arguments) {
    char* filename;
    FILE* in;

    filename = car(arguments)->data.string.value;
    in = fopen(filename, "r");
    if (in == NULL) {
        fprintf(stderr, "*** could not open file \"%s\"\n", filename);
        exit(1);
    }
    return make_input_port(in);
}

object* close_input_port_proc(object* arguments) {
    int result;

    result = fclose(car(arguments)->data.input_port.stream);
    if (result == EOF) {
        fprintf(stderr, "*** could not close input port\n");
        exit(1);
    }
    return ok_symbol;
}

char is_input_port(object* obj);

object* is_input_port_proc(object* arguments) {
    return is_input_port(car(arguments)) ? true : false;
}

object* read_proc(object* arguments) {
    FILE* in;
    object* result;

    in = is_nil(arguments) ?
        stdin :
        car(arguments)->data.input_port.stream;
    result = sread(in);
    return (result == NULL) ? eof_object : result;
}

object* read_char_proc(object* arguments) {
    FILE* in;
    int result;

    in = is_nil(arguments) ?
        stdin :
        car(arguments)->data.input_port.stream;
    result = getc(in);
    return (result == EOF) ? eof_object : make_character(result);
}

int peek(FILE* in);

object* peek_char_proc(object* arguments) {
    FILE* in;
    int result;

    in = is_nil(arguments) ?
        stdin :
        car(arguments)->data.input_port.stream;
    result = peek(in);
    return (result == EOF) ? eof_object : make_character(result);
}

char is_eof_object(object* obj);

object* is_eof_object_proc(object* arguments) {
    return is_eof_object(car(arguments)) ? true : false;
}

object* make_output_port(FILE* in);

object* open_output_port_proc(object* arguments) {
    char* filename;
    FILE* out;

    filename = car(arguments)->data.string.value;
    out = fopen(filename, "w");
    if (out == NULL) {
        fprintf(stderr, "*** could not open file \"%s\"\n", filename);
        exit(1);
    }
    return make_output_port(out);
}

object* close_output_port_proc(object* arguments) {
    int result;

    result = fclose(car(arguments)->data.output_port.stream);
    if (result == EOF) {
        fprintf(stderr, "*** could not close output port\n");
        exit(1);
    }
    return ok_symbol;
}

char is_output_port(object* obj);

object* is_output_port_proc(object* arguments) {
    return is_output_port(car(arguments)) ? true : false;
}

object* write_char_proc(object* arguments) {
    object* character;
    FILE* out;

    character = car(arguments);
    arguments = cdr(arguments);
    out = is_nil(arguments) ?
        stdout :
        car(arguments)->data.output_port.stream;
    putc(character->data.character.value, out);
    fflush(out);
    return ok_symbol;
}

void swrite(FILE* out, object* obj);

object* write_proc(object* arguments) {
    object* exp;
    FILE* out;

    exp = car(arguments);
    arguments = cdr(arguments);
    out = is_nil(arguments) ?
        stdout :
        car(arguments)->data.output_port.stream;
    swrite(out, exp);
    fflush(out);
    return ok_symbol;
}

object* error_proc(object* arguments) {
    while (!is_nil(arguments)) {
        swrite(stderr, car(arguments));
        fprintf(stderr, " ");
        arguments = cdr(arguments);
    };
    printf("\n*** exiting\n");
    exit(1);
}

object* make_compound_proc(object* params, object* body,
    object* env) {
    object* obj;

    obj = alloc_object();
    obj->type = COMPOUND_PROC;
    obj->data.compound_proc.params = params;
    obj->data.compound_proc.body = body;
    obj->data.compound_proc.env = env;
    return obj;
}

char is_compound_proc(object* obj) {
    return obj->type == COMPOUND_PROC;
}

object* make_input_port(FILE* stream) {
    object* obj;

    obj = alloc_object();
    obj->type = INPUT_PORT;
    obj->data.input_port.stream = stream;
    return obj;
}

char is_input_port(object* obj) {
    return obj->type == INPUT_PORT;
}

object* make_output_port(FILE* stream) {
    object* obj;

    obj = alloc_object();
    obj->type = OUTPUT_PORT;
    obj->data.output_port.stream = stream;
    return obj;
}

char is_output_port(object* obj) {
    return obj->type == OUTPUT_PORT;
}

char is_eof_object(object* obj) {
    return obj == eof_object;
}


/****** FINISH PROCS *********/

object* enclosing_env(object* env) {
    return cdr(env);
}

object* first_frame(object* env) {
    return car(env);
}

object* make_frame(object* var, object* val) {
    return cons(var, val);
}

object* frame_var(object* frame) {
    return car(frame);
}

object* frame_val(object* frame) {
    return cdr(frame);
}

void add_to_frame(object* var, object* val, object* frame) {
    set_car(frame, cons(var, car(frame)));
    set_cdr(frame, cons(val, cdr(frame)));
}

object* extend_env(object* vars, object* vals, object* base_env) {
    return cons(make_frame(vars, vals), base_env);
}

object* lookup_var_val(object* var, object* env) {
    object* frame;
    object* vars;
    object* vals;
    while (!is_nil(env)) {
        frame = first_frame(env);
        vars = frame_var(frame);
        vals = frame_val(frame);
        while (!is_nil(vars)) {
            if (var == car(vars)) {
                return car(vals);
            }
            vars = cdr(vars);
            vals = cdr(vals);
        }
        env = enclosing_env(env);
    }
    fprintf(stderr, "*** unbound variable, %s\n", var->data.symbol.value);
    exit(1);
}

void set_var_val(object* var, object* val, object* env) {
    object* frame;
    object* vars;
    object* vals;

    while (!is_nil(env)) {
        frame = first_frame(env);
        vars = frame_var(frame);
        vals = frame_val(frame);
        while (!is_nil(vars)) {
            if (var == car(vars)) {
                set_car(vals, val);
                return;
            }
            vars = cdr(vars);
            vals = cdr(vals);
        }
        env = enclosing_env(env);
    }
    fprintf(stderr, "*** unbound variable, %s\n", var->data.symbol.value);
    exit(1);
}

void define_var(object* var, object* val, object* env) {
    object* frame;
    object* vars;
    object* vals;

    frame = first_frame(env);
    vars = frame_var(frame);
    vals = frame_val(frame);

    while (!is_nil(vars)) {
        if (var == car(vars)) {
            set_car(vals, val);
            return;
        }
        vars = cdr(vars);
        vals = cdr(vals);
    }
    add_to_frame(var, val, frame);
}

object* setup_env(void) {
    object* initial_env;

    initial_env = extend_env(
        nil,
        nil,
        the_empty);
    return initial_env;
}

void populate_environment(object* env) {

    /* Primitive functions */
    add_procedure("null?", is_null_proc);
    add_procedure("boolean?", is_boolean_proc);
    add_procedure("symbol?", is_symbol_proc);
    add_procedure("integer?", is_integer_proc);
    add_procedure("real?", is_real_proc);
    add_procedure("complex?", is_complex_proc);
    add_procedure("char?", is_char_proc);
    add_procedure("string?", is_string_proc);
    add_procedure("pair?", is_pair_proc);
    add_procedure("procedure?", is_procedure_proc);

    add_procedure("char->integer", char_to_integer_proc);
    add_procedure("integer->char", integer_to_char_proc);
    add_procedure("number->string", number_to_string_proc);
    add_procedure("string->number", string_to_number_proc);
    add_procedure("symbol->string", symbol_to_string_proc);
    add_procedure("string->symbol", string_to_symbol_proc);

    add_procedure("+", add_proc);
    add_procedure("-", sub_proc);
    add_procedure("*", mul_proc);
    add_procedure("/", div_proc);
    add_procedure("quotient", quotient_proc);
    add_procedure("remainder", remainder_proc);
    add_procedure("=", is_numbeq_proc);
    add_procedure("<", is_lessthan_proc);
    add_procedure(">", is_greatthan_proc);

    add_procedure("cons", cons_proc);
    add_procedure("car", car_proc);
    add_procedure("cdr", cdr_proc);
    add_procedure("set-car!", set_car_proc);
    add_procedure("set-cdr!", set_cdr_proc);
    add_procedure("list", list_proc);

    add_procedure("eq?", is_eq_proc);

    add_procedure("apply", apply_proc);

    add_procedure("interaction-environment",
        interaction_environment_proc);
    add_procedure("null-environment", null_environment_proc);
    add_procedure("environment", environment_proc);
    add_procedure("eval", eval_proc);

    add_procedure("load", load_proc);
    add_procedure("open-input-port", open_input_port_proc);
    add_procedure("close-input-port", close_input_port_proc);
    add_procedure("input-port?", is_input_port_proc);
    add_procedure("read", read_proc);
    add_procedure("read-char", read_char_proc);
    add_procedure("peek-char", peek_char_proc);
    add_procedure("eof-object?", is_eof_object_proc);
    add_procedure("open-output-port", open_output_port_proc);
    add_procedure("close-output-port", close_output_port_proc);
    add_procedure("output-port?", is_output_port_proc);
    add_procedure("write-char", write_char_proc);
    add_procedure("write", write_proc);

    add_procedure("error", error_proc);

    add_procedure("gc", gc_proc);
    add_procedure("gc-stats", gc_stats_proc);
}

object* make_environment(void) {
    object* env;

    env = setup_env();
    populate_environment(env);
    return env;
}

void init(void) {

    the_vm = newVM();

    nil = alloc_object();
    nil->type = THE_NIL;

    false = alloc_object();
    false->type = BOOLEAN;
    false->data.boolean.value = 0;

    true = alloc_object();
    true->type = BOOLEAN;
    true->data.boolean.value = 1;

    symtab = nil;
    quote_symbol = make_symbol("quote");
    define_symbol = make_symbol("define");
    set_symbol = make_symbol("set!");
    ok_symbol = make_symbol("ok");
    if_symbol = make_symbol("if");
    lambda_symbol = make_symbol("lambda");
    begin_symbol = make_symbol("begin");
    cond_symbol = make_symbol("cond");
    else_symbol = make_symbol("else");
    let_symbol = make_symbol("let");
    and_symbol = make_symbol("and");
    or_symbol = make_symbol("or");

    eof_object = alloc_object();
    eof_object->type = EOF_OBJECT;

    the_empty = nil;

    the_global = make_environment();
}

/***************************** READ ******************************/

char is_delimiter(int c) {
    return isspace(c) || c == EOF ||
        c == '(' || c == ')' ||
        c == '"' || c == ';';
}

char is_initial(int c) {
    return isalpha(c) || c == '*' || c == '/' || c == '>' ||
        c == '<' || c == '=' || c == '?' || c == '!';
}

int peek(FILE* in) {
    int c;

    c = getc(in);
    ungetc(c, in);
    return c;
}

void eat_whitespace(FILE* in) {
    int c;

    while ((c = getc(in)) != EOF) {
        if (isspace(c)) {
            continue;
        }
        else if (c == ';') { /* comments are whitespace also */
            while (((c = getc(in)) != EOF) && (c != '\n'));
            continue;
        }
        ungetc(c, in);
        break;
    }
}

void eat_expected_string(FILE* in, char* str) {
    int c;

    while (*str != '\0') {
        c = getc(in);
        if (c != *str) {
            fprintf(stderr, "unexpected character '%c'\n", c);
            exit(1);
        }
        str++;
    }
}

void peek_expected_delimiter(FILE* in) {
    if (!is_delimiter(peek(in))) {
        fprintf(stderr, "character not followed by delimiter\n");
        exit(1);
    }
}

object* read_character(FILE* in) {
    int c;

    c = getc(in);
    switch (c) {
    case EOF:
        fprintf(stderr, "incomplete character literal\n");
        exit(1);
    case 's':
        if (peek(in) == 'p') {
            eat_expected_string(in, "pace");
            peek_expected_delimiter(in);
            return make_character(' ');
        }
        break;
    case 'n':
        if (peek(in) == 'e') {
            eat_expected_string(in, "ewl");
            peek_expected_delimiter(in);
            return make_character('\n');
        }
        break;
    }
    peek_expected_delimiter(in);
    return make_character(c);
}

object* read_number(FILE* in) {
    int c;
    short sign = 1;
    short isflo = 0;
    short mant_length = 1;
    double mant = 0.0;
    double dnum = 0.0;
    long num = 0;

    c = getc(in);
    
    /* read a fixnum */
    if (c == '-') {
        sign = -1;
    }
    else {
        ungetc(c, in);
    }
    while (isdigit(c = getc(in))) {
        num = (num * 10) + (c - '0');
    }

    if (c == '.') {
        /* flonum */
        while (isdigit(c = getc(in))) {
            double m = (double)(c)-'0';
            mant += m / pow(10.0, mant_length);
            mant_length++;
        }
        isflo = 1;
        dnum = sign * ((double)num + mant);
    }

    num *= sign;

    if (is_delimiter(c)) {
        ungetc(c, in);
        if (isflo) {
            return make_flonum(dnum);
        }
        else {
            return make_fixnum(num);
        }
    }
    else {
        fprintf(stderr, "number not followed by delimiter\n");
        exit(1);
    }
}

object* read_complex(FILE* in) {
    int c;
    object* num;
    double re;
    double im;

    c = getc(in);
    if (c == '(') {
        /* Complex number */
        eat_whitespace(in);
        if (isdigit(peek(in))) {
            num = alloc_object();
            num = read_number(in);
            if (num->type == FIXNUM) {
                re = (double)num->data.fixnum.value;
            }
            else if (num->type == FLONUM) {
                re = num->data.flonum.value;
            }
            else {
                fprintf(stderr, "*** invalid number type for real part\n");
                exit(1);
            }
        }
        else {
            fprintf(stderr, "*** there must be a real part\n");
            exit(1);
        }
        eat_whitespace(in);
        if (isdigit(peek(in))) {
            num = alloc_object();
            num = read_number(in);
            if (num->type == FIXNUM) {
                im = (double)num->data.fixnum.value;
            }
            else if (num->type == FLONUM) {
                im = num->data.flonum.value;
            }
            else {
                fprintf(stderr, "*** invalid number type for imaginary part\n");
                exit(1);
            }
        }
        else {
            fprintf(stderr, "*** invalid complex number. No imaginary part\n");
            exit(1);
        }
        c = getc(in);
        if (c != ')') {
            fprintf(stderr, "*** missing parens closing the complex number\n");
            exit(1);
        }
    }
    else {
        fprintf(stderr, "*** invalid complex number\n");
        exit(1);
    }
    return make_cpxnum(re, im);
}

object* read_pair(FILE* in) {
    int c;
    object* car_obj;
    object* cdr_obj;

    eat_whitespace(in);

    c = getc(in);
    if (c == ')') {
        /* the nil */
        return nil;
    }
    ungetc(c, in);

    car_obj = sread(in);

    eat_whitespace(in);

    c = getc(in);
    if (c == '.') {
        /* read improper list */
        c = peek(in);
        if (!is_delimiter(c)) {
            fprintf(stderr, "*** do not followed by delimiter\n");
            exit(1);
        }
        cdr_obj = sread(in);
        eat_whitespace(in);
        c = getc(in);
        if (c != ')') {
            fprintf(stderr, "*** where was the trailing right paren?\n");
            exit(1);
        }
        return cons(car_obj, cdr_obj);
    }
    else {
        ungetc(c, in);
        cdr_obj = read_pair(in);
        return cons(car_obj, cdr_obj);
    }
}

object* sread(FILE* in) {
    int c;
    short sign = 1;
    short isflo = 0;
    short mant_length = 1;
    double mant = 0.0;
    double dnum = 0.0;
    long num = 0;
    char buffer[BUFFER_MAX];

    eat_whitespace(in);

    c = getc(in);

    if (c == '#') { /* read a boolean or character or complex number */
        c = getc(in);
        switch (c) {
        case 't':
            return true;
        case 'f':
            return false;
        case '\\':
            return read_character(in);
        case 'c': /* LISP STYLE: not so SCHEME */
            return read_complex(in);
        default:
            fprintf(stderr,
                "unknown boolean or character literal\n");
            exit(1);
        }
    }
    else if (isdigit(c) || (c == '-' && (isdigit(peek(in))))) {
        /* read a fixnum */
        if (c == '-') {
            sign = -1;
        }
        else {
            ungetc(c, in);
        }
        while (isdigit(c = getc(in))) {
            num = (num * 10) + (c - '0');
        }
        
        if (c == '.') {
            /* flonum */
            while (isdigit(c = getc(in))) {
                double m = (double)(c) - '0';
                mant += m / pow(10.0,mant_length);
                mant_length++;
            }
            isflo = 1;
            dnum = sign * ((double)num + mant);
        }

        num *= sign;

        if (is_delimiter(c)) {
            ungetc(c, in);
            if (isflo) {
                return make_flonum(dnum);
            }
            else {
                return make_fixnum(num);
            }
        }
        else {
            fprintf(stderr, "number not followed by delimiter\n");
            exit(1);
        }
    }
    else if (is_initial(c) ||
        ((c == '+' || c == '-') &&
            is_delimiter(peek(in)))) {
        /* reading a symbol */
        int i = 0;
        while (is_initial(c) || isdigit(c) ||
            c == '+' || c == '-') {
            if (i < BUFFER_MAX - 1) {
                buffer[i++] = c;
            }
            else {
                fprintf(stderr, "*** symbol too long. "
                    "Maximum length is %d\n", BUFFER_MAX);
                exit(1);
            }
            c = getc(in);
        }
        if (is_delimiter(c)) {
            buffer[i] = '\0';
            ungetc(c, in);
            return make_symbol(buffer);
        }
        else {
            fprintf(stderr, "*** symbol not followed by delimiter. "
                "Found '%c'\n", c);
            exit(1);
        }
    }
    else if (c == '"') {
        /* read string */
        int i = 0;
        while ((c = getc(in)) != '"') {
            if (c == '\\') {
                c = getc(in);
                if (c == 'n') {
                    c = '\n';
                }
            }
            if (c == EOF) {
                fprintf(stderr, "*** non-terminated string literal\n");
                exit(1);
            }
            /* save space for string terminator */
            if (i < BUFFER_MAX - 1) {
                buffer[i++] = c;
            }
            else {
                fprintf(stderr,
                    "*** string too long. Maximum length is %d\n",
                    BUFFER_MAX);
                exit(1);
            }
        }
        buffer[i] = '\0';
        return make_string(buffer);
    }
    else if (c == '(') {
        return read_pair(in);
    }
    else if (c == '\'') {
        return cons(quote_symbol, cons(sread(in), nil));
    }
    else if (c == EOF) {
        return NULL;
    }
    else {
        fprintf(stderr, "bad input. Unexpected '%c'\n", c);
        exit(1);
    }
    fprintf(stderr, "read illegal state\n");
    exit(1);
}

/*************************** EVALUATE ****************************/

char is_self_eval(object* exp) {
    return is_boolean(exp) ||
        is_fixnum(exp) ||
        is_flonum(exp) ||
        is_cpxnum(exp) ||
        is_character(exp) ||
        is_string(exp);
}

char is_variable(object* exp) {
    return is_symbol(exp);
}

char is_tagged_list(object* exp, object* tag) {
    object* the_car;

    if (is_pair(exp)) {
        the_car = car(exp);
        return is_symbol(the_car) && (the_car == tag);
    }
    return 0;
}

char is_quoted(object* exp) {
    return is_tagged_list(exp, quote_symbol);
}

object* txt_quote(object* exp) {
    /* text of quotation */
    return cadr(exp);
}

char is_assignment(object* exp) {
    return is_tagged_list(exp, set_symbol);
}

object* assign_var(object* exp) {
    /* it's tagged. the CAR is the tag. */
    return car(cdr(exp));
}

object* assign_val(object* exp) {
    return car(cdr(cdr(exp)));
}

char is_definition(object* exp) {
    return is_tagged_list(exp, define_symbol);
}

object* definition_var(object* exp) {
    if (is_symbol(cadr(exp))) {
        return cadr(exp);
    }
    else {
        return caadr(exp);
    }
}

object* make_lambda(object* params, object* body); /* forward declaration */

object* definition_val(object* exp) {
    if (is_symbol(cadr(exp))) {
        return caddr(exp);
    }
    else {
        return make_lambda(cdadr(exp), cddr(exp));
    }
}

object* make_if(object* predicate, object* consequent,
    object* alternative) {
    return cons(if_symbol,
        cons(predicate,
            cons(consequent,
                cons(alternative, nil))));
}

char is_if(object* exp) {
    return is_tagged_list(exp, if_symbol);
}

object* if_pred(object* exp) {
    return cadr(exp);
}

object* if_cons(object* exp) {
    return caddr(exp);
}

object* if_alt(object* exp) {
    if (is_nil(cdddr(exp))) {
        return false;
    }
    else {
        return cadddr(exp);
    }
}

object* make_lambda(object* params, object* body) {
    return cons(lambda_symbol, cons(params, body));
}

char is_lambda(object* exp) {
    return is_tagged_list(exp, lambda_symbol);
}

object* lambda_params(object* exp) {
    return cadr(exp);
}

object* lambda_body(object* exp) {
    return cddr(exp);
}

object* make_begin(object* seq) {
    return cons(begin_symbol, seq);
}

char is_last_exp(object* seq) {
    return is_nil(cdr(seq));
}

object* first_exp(object* seq) {
    return car(seq);
}

object* rest_exps(object* seq) {
    return cdr(seq);
}

char is_cond(object* exp) {
    return is_tagged_list(exp, cond_symbol);
}

object* cond_clauses(object* exp) {
    return cdr(exp);
}

object* cond_predicate(object* clause) {
    return car(clause);
}

object* cond_actions(object* clause) {
    return cdr(clause);
}

char is_cond_else_clause(object* clause) {
    return cond_predicate(clause) == else_symbol;
}

object* sequence_to_exp(object* seq) {
    if (is_nil(seq)) {
        return seq;
    }
    else if (is_last_exp(seq)) {
        return first_exp(seq);
    }
    else {
        return make_begin(seq);
    }
}

object* expand_clauses(object* clauses) {
    object* first;
    object* rest;

    if (is_nil(clauses)) {
        return false;
    }
    else {
        first = car(clauses);
        rest = cdr(clauses);
        if (is_cond_else_clause(first)) {
            if (is_nil(rest)) {
                return sequence_to_exp(cond_actions(first));
            }
            else {
                fprintf(stderr, "*** else clause isn't last cond->if");
                exit(1);
            }
        }
        else {
            return make_if(cond_predicate(first),
                sequence_to_exp(cond_actions(first)),
                expand_clauses(rest));
        }
    }
}

object* cond_to_if(object* exp) {
    return expand_clauses(cond_clauses(exp));
}

object* make_application(object* operator, object* operands) {
    return cons(operator, operands);
}

char is_application(object* exp) {
    return is_pair(exp);
}

char is_begin(object* exp) {
    return is_tagged_list(exp, begin_symbol);
}

object* begin_actions(object* exp) {
    return cdr(exp);
}

object* operator(object* exp) {
    return car(exp);
}

object* operands(object* exp) {
    return cdr(exp);
}

char is_no_operands(object* ops) {
    return is_nil(ops);
}

object* first_operand(object* ops) {
    return car(ops);
}

object* rest_operands(object* ops) {
    return cdr(ops);
}

char is_let(object* exp) {
    return is_tagged_list(exp, let_symbol);
}

object* let_bindings(object* exp) {
    return cadr(exp);
}

object* let_body(object* exp) {
    return cddr(exp);
}

object* binding_parameter(object* binding) {
    return car(binding);
}

object* binding_argument(object* binding) {
    return cadr(binding);
}

object* bindings_parameters(object* bindings) {
    return is_nil(bindings) ?
        nil :
        cons(binding_parameter(car(bindings)),
            bindings_parameters(cdr(bindings)));
}

object* bindings_arguments(object* bindings) {
    return is_nil(bindings) ?
        nil :
        cons(binding_argument(car(bindings)),
            bindings_arguments(cdr(bindings)));
}

object* let_parameters(object* exp) {
    return bindings_parameters(let_bindings(exp));
}

object* let_arguments(object* exp) {
    return bindings_arguments(let_bindings(exp));
}

object* let_to_application(object* exp) {
    return make_application(
        make_lambda(let_parameters(exp),
            let_body(exp)),
        let_arguments(exp));
}

char is_and(object* exp) {
    return is_tagged_list(exp, and_symbol);
}

object* and_tests(object* exp) {
    return cdr(exp);
}

char is_or(object* exp) {
    return is_tagged_list(exp, or_symbol);
}

object* or_tests(object* exp) {
    return cdr(exp);
}

object* apply_operator(object* arguments) {
    return car(arguments);
}

object* prepare_apply_operands(object* arguments) {
    if (is_nil(cdr(arguments))) {
        return car(arguments);
    }
    else {
        return cons(car(arguments),
            prepare_apply_operands(cdr(arguments)));
    }
}

object* apply_operands(object* arguments) {
    return prepare_apply_operands(cdr(arguments));
}

object* eval_expression(object* arguments) {
    return car(arguments);
}

object* eval_environment(object* arguments) {
    return cadr(arguments);
}

object* list_of_values(object* exps, object* env) {
    if (is_no_operands(exps)) {
        return nil;
    }
    else {
        return cons(eval(first_operand(exps), env),
            list_of_values(rest_operands(exps), env));
    }
}

object* eval_assignment(object* exp, object* env) {
    set_var_val(assign_var(exp), eval(assign_val(exp), env), env);
    return ok_symbol;
}

object* eval_def(object* exp, object* env) {
    define_var(definition_var(exp), eval(definition_val(exp), env), env);
    return ok_symbol;
}

/* Tail call recursion */
object* eval(object* exp, object* env) {
    object* proc;
    object* args;
    object* result;

tailcall:
    if (is_self_eval(exp)) {
        return exp;
    }
    else if (is_variable(exp)) {
        return lookup_var_val(exp, env);
    }
    else if (is_quoted(exp)) {
        return txt_quote(exp);
    }
    else if (is_assignment(exp)) {
        return eval_assignment(exp, env);
    }
    else if (is_definition(exp)) {
        return eval_def(exp, env);
    }
    else if (is_if(exp)) {
        exp = is_true(eval(if_pred(exp), env)) ?
            if_cons(exp) :
            if_alt(exp);
        goto tailcall;
    }
    else if (is_lambda(exp)) {
        return make_compound_proc(lambda_params(exp),
            lambda_body(exp), env);
    }
    else if (is_begin(exp)) {
        exp = begin_actions(exp);
        while (!is_last_exp(exp)) {
            eval(first_exp(exp), env);
            exp = rest_exps(exp);
        }
        exp = first_exp(exp);
        goto tailcall;
    }
    else if (is_cond(exp)) {
        exp = cond_to_if(exp); /* according to SICP */
        goto tailcall;
    }
    else if (is_let(exp)) {
        exp = let_to_application(exp);
        goto tailcall;
    }
    else if (is_and(exp)) {
        exp = and_tests(exp);
        if (is_nil(exp)) {
            return true;
        }
        while (!is_last_exp(exp)) {
            result = eval(first_exp(exp), env);
            if (is_false(result)) {
                return result;
            }
            exp = rest_exps(exp);
        }
        exp = first_exp(exp);
        goto tailcall;
    }
    else if (is_or(exp)) {
        exp = or_tests(exp);
        if (is_nil(exp)) {
            return false;
        }
        while (!is_last_exp(exp)) {
            result = eval(first_exp(exp), env);
            if (is_true(result)) {
                return result;
            }
            exp = rest_exps(exp);
        }
        exp = first_exp(exp);
        goto tailcall;
    }
    else if (is_application(exp)) {
        proc = eval(operator(exp), env);
        args = list_of_values(operands(exp), env);

        /* handle eval specially for tail call requirement */
        if (is_primitive(proc) &&
            proc->data.primitive_proc.fn == eval_proc) {
            exp = eval_expression(args);
            env = eval_environment(args);
            goto tailcall;
        }

        /* handle apply specially for tailcall requirement */
        if (is_primitive(proc) &&
            proc->data.primitive_proc.fn == apply_proc) {
            proc = apply_operator(args);
            args = apply_operands(args);
        }
        
        if (is_primitive(proc)) {
            return (proc->data.primitive_proc.fn)(args);
        }
        else if (is_compound_proc(proc)) {
            env = extend_env(proc->data.compound_proc.params,
                args,
                proc->data.compound_proc.env);
            exp = make_begin(proc->data.compound_proc.body);
            goto tailcall;
        }
    }
    else {
        fprintf(stderr, "*** cannot eval unknown expression type\n");
        exit(1);
    }
    fprintf(stderr, "*** eval illegal state\n");
    exit(1);
}

/**************************** PRINT ******************************/

void write_pair(FILE* out, object* pair) {
    object* car_obj;
    object* cdr_obj;

    car_obj = car(pair);
    cdr_obj = cdr(pair);
    swrite(out, car_obj);
    if (cdr_obj->type == PAIR) { /* nested pairs/lists */
        fprintf(out, " ");
        write_pair(out, cdr_obj);
    }
    else if (cdr_obj->type == THE_NIL) {
        return;
    }
    else {
        fprintf(out, " . ");
        swrite(out, cdr_obj);
    }
}

void swrite(FILE* out, object* obj) {
    char c;
    char* str;

    switch (obj->type) {
    case THE_NIL:
        fprintf(out, "()");
        break;
    case BOOLEAN:
        fprintf(out, "#%c", is_false(obj) ? 'f' : 't');
        break;
    case SYMBOL:
        fprintf(out, "%s", obj->data.symbol.value);
        break;
    case FIXNUM:
        fprintf(out, "%ld", obj->data.fixnum.value);
        break;
    case FLONUM:
        fprintf(out, "%lf", obj->data.flonum.value);
        break;
    case CPXNUM:
        if (cimag(obj->data.cpxnum.value) == 0.0) {
            fprintf(out, "%lf", creal(obj->data.cpxnum.value));
        }
        else {
            fprintf(out, "#C(%lf %lf)", creal(obj->data.cpxnum.value),
                cimag(obj->data.cpxnum.value));
        }
        break;
    case STRING:
        str = obj->data.string.value;
        putc('"', out);
        while (*str != '\0') {
            switch (*str) {
            case '\n':
                fprintf(out, "\\n");
                break;
            case '\\':
                fprintf(out, "\\\\");
                break;
            case '"':
                fprintf(out, "\\\"");
                break;
            default:
                putc(*str, out);
            }
            str++;
        }
        putc('"', out);
        break;
    case CHARACTER:
        c = obj->data.character.value;
        fprintf(out, "#\\");
        switch (c) {
        case '\n':
            fprintf(out, "newl");
            break;
        case ' ':
            fprintf(out, "space");
            break;
        default:
            putc(c, out);
        }
        break;
    case PAIR:
        fprintf(out, "(");
        write_pair(out, obj);
        fprintf(out, ")");
        break;
    case COMPOUND_PROC:
        fprintf(out, "#<comound-procedure: %p>", obj);
        break;
    case PRIMITIVE_PROC:
        fprintf(out, "#<primitive-procedure: %p>", obj);
        break;
    case INPUT_PORT:
        fprintf(out, "#<input-port>");
        break;
    case OUTPUT_PORT:
        fprintf(out, "#<output-port>");
        break;
    case EOF_OBJECT:
        fprintf(out, "#<eof>");
        break;
    default:
        fprintf(stderr, "cannot write unknown type\n");
        exit(1);
    }
}

/***************************** REPL ******************************/

int main(void) {
    object* exp;

    printf("Welcome to Bootstrap Scheme. "
        "Use ctrl-c to exit.\n");

    init();

    while (1) {
        printf("> ");
        exp = sread(stdin);
        if (exp == NULL) {
            break;
        }
        swrite(stdout, eval(exp, the_global));
        printf("\n");
    }

    printf("Goodbye\n");

    return 0;
}

/**************************** MUSIC *******************************

Slipknot, Neil Young, Pearl Jam, The Dead Weather,
Dave Matthews Band, Alice in Chains, White Zombie, Blind Melon,
Priestess, Puscifer, Bob Dylan, Them Crooked Vultures

*/