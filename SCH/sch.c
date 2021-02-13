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

#define BUFFER_MAX 1000 /* max string length */

#define add_procedure(scheme_name, c_name)    \
    define_var(make_symbol(scheme_name),      \
                    make_primitive(c_name),   \
                    the_global);

 /**************************** MODEL ******************************/

typedef enum {
    BOOLEAN, FIXNUM, CHARACTER, FLONUM,
    CPXNUM, STRING, PAIR, THE_NIL, SYMBOL,
    PRIMITIVE_PROC
} object_type;

#if defined(_MSC_VER)
typedef _Dcomplex sComplex;
#else
typedef double complex sComplex;
#endif

typedef struct object {
    object_type type;
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
    } data;
} object;

/* no GC so truely "unlimited extent" */
object* alloc_object(void) {
    object* obj;

    obj = malloc(sizeof(object));
    if (obj == NULL) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    return obj;
}

object * false;
object * true;
object* nil;
object* symtab;

/**************** SYMBOL DEFINITION ***********/

object* quote_symbol;
object* set_symbol;
object* define_symbol;
object* ok_symbol;
object* if_symbol;

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
    return obj;
}

object* make_cpxnum2(sComplex z) {
    object* obj;

    obj = alloc_object();
    obj->type = CPXNUM;
    obj->data.cpxnum.value = z;
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

object* is_procedure_proc(object* arguments) {
    return is_primitive(car(arguments)) ? true : false;
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
    fprintf(stderr, "*** unbound variable\n");
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
    fprintf(stderr, "*** unbound variable\n");
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

void init(void) {
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

    the_empty = nil;

    the_global = setup_env();

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

object* sread(FILE* in); /* forward declaration */

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
    return cadr(exp);
}

object* definition_val(object* exp) {
    return caddr(exp);
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

char is_application(object* exp) {
    return is_pair(exp);
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

object* eval(object* exp, object* env); /* forward declaration */

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
    else if (is_application(exp)) {
        proc = eval(operator(exp), env);
        args = list_of_values(operands(exp), env);
        return (proc->data.primitive_proc.fn)(args);
    }
    else {
        fprintf(stderr, "*** cannot eval unknown expression type\n");
        exit(1);
    }
    fprintf(stderr, "*** eval illegal state\n");
    exit(1);
}

/**************************** PRINT ******************************/
void swrite(object* obj); /* forward declaration */

void write_pair(object* pair) {
    object* car_obj;
    object* cdr_obj;

    car_obj = car(pair);
    cdr_obj = cdr(pair);
    swrite(car_obj);
    if (cdr_obj->type == PAIR) { /* nested pairs/lists */
        printf(" ");
        write_pair(cdr_obj);
    }
    else if (cdr_obj->type == THE_NIL) {
        return;
    }
    else {
        printf(" . ");
        swrite(cdr_obj);
    }
}

void swrite(object* obj) {
    char c;
    char* str;

    switch (obj->type) {
    case THE_NIL:
        printf("()");
        break;
    case BOOLEAN:
        printf("#%c", is_false(obj) ? 'f' : 't');
        break;
    case SYMBOL:
        printf("%s", obj->data.symbol.value);
        break;
    case FIXNUM:
        printf("%ld", obj->data.fixnum.value);
        break;
    case FLONUM:
        printf("%lf", obj->data.flonum.value);
        break;
    case CPXNUM:
        if (cimag(obj->data.cpxnum.value) == 0.0) {
            printf("%lf", creal(obj->data.cpxnum.value));
        }
        else {
            printf("#C(%lf %lf)", creal(obj->data.cpxnum.value),
                cimag(obj->data.cpxnum.value));
        }
        break;
    case STRING:
        str = obj->data.string.value;
        putchar('"');
        while (*str != '\0') {
            switch (*str) {
            case '\n':
                printf("\\n");
                break;
            case '\\':
                printf("\\\\");
                break;
            case '"':
                printf("\\\"");
                break;
            default:
                putchar(*str);
            }
            str++;
        }
        putchar('"');
        break;
    case CHARACTER:
        c = obj->data.character.value;
        printf("#\\");
        switch (c) {
        case '\n':
            printf("newl");
            break;
        case ' ':
            printf("space");
            break;
        default:
            putchar(c);
        }
        break;
    case PAIR:
        printf("(");
        write_pair(obj);
        printf(")");
        break;
    case PRIMITIVE_PROC:
        printf("#<procedure: %p>", obj);
        break;
    default:
        fprintf(stderr, "cannot write unknown type\n");
        exit(1);
    }
}

/***************************** REPL ******************************/

int main(void) {

    printf("Welcome to Bootstrap Scheme. "
        "Use ctrl-c to exit.\n");

    init();

    while (1) {
        printf("> ");
        swrite(eval(sread(stdin), the_global));
        printf("\n");
    }

    return 0;
}

/**************************** MUSIC *******************************

Slipknot, Neil Young, Pearl Jam, The Dead Weather,
Dave Matthews Band, Alice in Chains, White Zombie, Blind Melon,
Priestess, Puscifer, Bob Dylan, Them Crooked Vultures

*/