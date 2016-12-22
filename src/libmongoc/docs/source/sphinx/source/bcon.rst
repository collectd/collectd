BCON - BSON C Object Notation
=============================

Overview
--------
BCON provides for JSON-like (or BSON-like) initializers in C.
Without this, BSON must be constructed by procedural coding via explicit function calls.
With this, you now have concise, readable, and maintainable data-driven definition of BSON documents.
Here are a couple of introductory examples.

.. code-block:: c

    bcon hello[] = { "hello", "world", "." };
    bcon pi[] = { "pi", BF(3.14159), BEND };

BCON is an array of bcon union elements with the default type of char* cstring.
A BCON document must be terminated with a char* cstring containing a single dot, i.e., ".", or the macro equivalent BEND.

Cstring literals in double quotes are used for keys as well as for string values.
There is no explicit colon (':') separator between key and value, just a comma,
however it must be explicit or C will quietly concatenate the key and value strings for you.
Readability may be improved by using multiple lines with a key-value pair per line.

Macros are used to enclose specific types, and an internal type-specifier string prefixes a typed value.
Macros are also used to specify interpolation of values from references (or pointers to references) of specified types.

Sub-documents are framed by "{" "}" string literals, and sub-arrays are framed by "[" "]" literals.

All of this is needed because C arrays and initializers are mono-typed unlike dict/array types in modern languages.
BCON attempts to be readable and JSON-like within the context and restrictions of the C language.

Important Note
--------------
BCON depends on C99 designated initializers.  The Microsoft Visual Studio (2012) C compiler does not yet support C99
designated initializers, so if you want BCON on Windows, you will have to install and use a compiler that supports C99
like gcc.

Specification
-------------
This specification parallels the BSON specification ( http://bsonspec.org/#/specification ).
The specific types and their corresponding macros are documented in the bcon (union bcon) structure.
The base values use two-character macros starting with "B" for the simple initialization using static values.

Examples
--------

.. code-block:: c

    bcon goodbye[] = { "hello", "world", "goodbye", "world", "." };
    bcon awesome[] = { "BSON", "[", "awesome", BF(5.05), BI(1986), "]", "." };
    bcon contact_info[] = {
        "firstName", "John",
        "lastName" , "Smith",
        "age"      , BI(25),
        "address"  ,
        "{",
            "streetAddress", "21 2nd Street",
            "city"         , "New York",
            "state"        , "NY",
            "postalCode"   , "10021",
        "}",
        "phoneNumber",
        "[",
            "{",
                "type"  , "home",
                "number", "212 555-1234",
            "}",
            "{",
                "type"  , "fax",
                "number", "646 555-4567",
            "}",
        "]",
        BEND
    };

Comparison
----------

JSON:

.. code-block:: javascript

    { "BSON" : [ "awesome", 5.05, 1986 ] }

BCON:

.. code-block:: c

    bcon awesome[] = { "BSON", "[", "awesome", BF(5.05), BI(1986), "]", BEND };

C driver calls:

.. code-block:: c

    bson_init( b );
    bson_append_start_array( b, "BSON" );
    bson_append_string( b, "0", "awesome" );
    bson_append_double( b, "1", 5.05 );
    bson_append_int( b, "2", 1986 );
    bson_append_finish_array( b );
    ret = bson_finish( b );
    bson_print( b );
    bson_destroy( b );

Performance
----------
With compiler optimization -O3, BCON costs about 1.1 to 1.2 times as much
as the equivalent bson function calls required to explicitly construct the document.
This is significantly less than the cost of parsing JSON and constructing BSON,
and BCON allows value interpolation via pointers.

Reference Interpolation
-----------------------
Reference interpolation uses three-character macros starting with "BR" for simple dynamic values.
You can change the referenced content and the new values will be interpolated when you generate BSON from BCON.

.. code-block:: c

    bson b[1];
    char name[] = "pi";
    double value = 3.14159;
    bcon bc[] = { "name", BRS(name), "value", BRF(&value), BEND };
    bson_from_bcon( b, bc ); // generates { name: "pi", "value", 3.14159 }
    strcpy(name, "e");
    value = 2.71828;
    bson_from_bcon( b, bc ); // generates { name: "pi", "value", 3.14159 }

Please remember that in C, the array type is anomalous in that an identifier is (already) a reference,
therefore there is no ampersand '&' preceding the identifier for reference interpolation.
This applies to BRS(cstring), BRD(doc), BRA(array), BRO(oid), and BRX(symbol).
An ampersand '&' is needed for value types BRF(&double), BRB(&boolean), BRT(&time), BRI(&int), and BRL(&long).
For completeness, BRS, BRD, BRA, BRO, and BRX are defined even though BS, BD, BA, BO, and BX are equivalent.

Pointer Interpolation
---------------------
Pointer(-to-reference) interpolation uses three-character macros starting with "BP" for **conditional** dynamic values.
You can change the pointer content and the new values will be interpolated when you generate BSON from BCON.
If you set the pointer to null, the element will skipped and not inserted into the generated BSON document.

.. code-block:: c

    bson b[1];
    char name[] = "pi";
    char new_name[] = "log(0)";
    char **pname = (char**)&name;
    double value = 3.14159;
    double *pvalue = &value;
    bcon bc[] = { "name", BPS(&pname), "value", BPF(&pvalue), BEND };
    bson_from_bcon( b, bc ); // generates { name: "pi", "value", 3.14159 }
    pname = (char**)&new_name;
    pvalue = 0;
    bson_from_bcon( b, bc ); // generates { name: "log(0)" }

Pointer interpolation necessarily adds an extra level of indirection and complexity.
All macro pointer arguments are preceded by '&'.
Underlying pointer types are double-indirect (**) for array types and single-indirect (*) for value types.
Char name[] is used above to highlight that the array reference is not assignable (in contrast to char* array).
Please note the (char**)& cast-address sequence required to silence the "incompatible-pointer-types" warning.

Additional Notes
----------------
Use the BS macro or the ":_s:" type specifier for string to allow string values that collide with type specifiers, braces, or square brackets.

.. code-block:: c

    bson b[1];
    bcon bc[] = { "spec", BS(":_s:"), BEND };
    bson_from_bcon( b, bc ); // generates { spec: ":_s:" }

BCON does not yet support the following BSON types.

=============================   ========================
element                         description
=============================   ========================
05  e_name  binary              Binary data
06  e_name                      undefined - deprecated
0B  e_name  cstring cstring     Regular expression
0C  e_name  string (byte*12)    DBPointer - Deprecated
0D  e_name  string              JavaScript code
0F  e_name  code_w_s            JavaScript code w/ scope
11  e_name  int64               Timestamp
FF  e_name                      Min key
7F  e_name                      Max key
=============================   ========================
