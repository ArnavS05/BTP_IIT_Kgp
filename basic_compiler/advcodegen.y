%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>

#define MAX_TYPES 512
#define MAX_TABLES 64
#define MAX_SYMBOLS 1024
#define MAX_INS 8192
#define TEXT_SIZE 64

enum {
    BASIC_TYPE,
    ARRAY_TYPE,
    STRUCT_TYPE
};

typedef struct {
    int category;
    int element;
    int count;
    int table;
    int size;
    int align;
    char name[128];
    char tag[64];
} type_t;

typedef struct {
    char name[64];
    int type;
    int offset;
} symbol_t;

typedef struct variable_t {
    char *name;
    int type;
} variable_t;

typedef struct expr_t {
    int type;
    char place[TEXT_SIZE];
} expr_t;

typedef struct address_t {
    int type;
    char place[TEXT_SIZE];
} address_t;

typedef struct list_node {
    int value;
    struct list_node *next;
} list_node;

typedef struct statement_t {
    list_node *nextlist;
} statement_t;

typedef struct boolean_t {
    list_node *truelist;
    list_node *falselist;
} boolean_t;

typedef struct struct_context_t {
    int old_table;
    int table;
    int type;
} struct_context_t;

typedef struct else_part_t {
    int has_else;
    int start;
    list_node *skip;
    list_node *nextlist;
} else_part_t;

typedef struct {
    int line;
    char op[32];
    char type[16];
    char dst[TEXT_SIZE];
    char arg1[TEXT_SIZE];
    char arg2[TEXT_SIZE];
} instruction_t;

type_t types[MAX_TYPES];
symbol_t symbols[MAX_TABLES][MAX_SYMBOLS];
int symbol_count[MAX_TABLES];
int table_size[MAX_TABLES];
int table_align[MAX_TABLES];
char table_name[MAX_TABLES][64];
int type_count=4;
int table_count =1;
int current_table=0;
int declaration_type=0;
int max_alignment=8;

instruction_t code[MAX_INS];
int instruction_count=0;
int temporary_count=1;
int had_error=0;

char input_path[512];
char output_directory[512];
char info_path[600];
char tac_path[600];

extern FILE *yyin;
extern int yylineno;
extern int yylex(void);

void yyerror(const char *text);
void lexer_error(const char *text);
char *copy_text(const char *text);

void compiler_error(int line,const char *format,...);
int align_up(int value,int alignment);
int type_alignment(int type);
int find_symbol(const char *name,int table);
void add_symbol(const char *name,int type,int table,int line);
int add_array_type(int element,int count,int line);
int find_struct_type(const char *name);
struct_context_t *begin_struct(const char *name,int line);
int finish_struct(struct_context_t *context);

const char *tac_type(int type);
int emit(int line,const char *op,const char *type,const char *dst,const char *a1,const char *a2);
void set_label(int instruction,int target);
void new_temp(char *out);
expr_t *new_expr(int type,const char *place);
address_t *new_address(int type,const char *place);
statement_t *new_statement(list_node *list);
boolean_t *new_boolean(list_node *true_list,list_node *false_list);

list_node *makelist(int value);
list_node *merge(list_node *first,list_node *second);
void backpatch(list_node *list,int target);

int common_type(int first,int second);
expr_t *cast_value(expr_t *value,int type,int line);
expr_t *binary_value(expr_t *left,expr_t *right,const char *op,int line);
expr_t *load_value(address_t *address,int line);
address_t *index_address(address_t *base,expr_t *index,int line);
address_t *member_address(address_t *base,address_t *member,int line);

void initialize_types(void);
void print_usage(FILE *file,const char *program);
int make_output_paths(const char *source);
int write_info_file(void);
int write_tac_file(void);
%}

%locations

%union {
    long long number;
    double real;
    char *text;
    int integer;
    struct variable_t *variable;
    struct expr_t *expression;
    struct address_t *address;
    struct statement_t *statement;
    struct boolean_t *boolean;
    struct struct_context_t *context;
    struct else_part_t *else_part;
}

%token INT LNG FLT DBL STRUCT IF ELSE WHILE
%token EQ NEQ LEQ GEQ NOT AND OR
%token <number> NUM
%token <real> FLTCONST
%token <text> ID

%left OR
%left AND
%right NOT
%left '+' '-'
%left '*' '/' '%'
%right NEG

%type <integer> BASIC DIM SET_BASIC END_STRUCT SET_STRUCT USE_STRUCT
%type <integer> ROOT MEMBER_TABLE M VARLIST
%type <context> BEGIN_STRUCT
%type <variable> VAR
%type <expression> EXPR TERM FACTOR
%type <address> ITEM SMPLITEM AREF
%type <statement> STMT STMTLIST N ASGN
%type <boolean> BOOL
%type <integer> RELOP
%type <else_part> ELSEPART

%start PROG

%%

PROG
    : DECLIST STMTLIST {
        backpatch($2->nextlist,instruction_count);
        emit(0,"HALT","none","-","-","-");
      }
    ;

DECLIST
    : DECLIST DECL
    | DECL
    ;

DECL
    : BASIC SET_BASIC VARLIST ';'
    | STRUCT ID BEGIN_STRUCT '{' DECLIST '}' END_STRUCT ';'
    | STRUCT ID BEGIN_STRUCT '{' DECLIST '}' END_STRUCT SET_STRUCT VARLIST ';'
    | STRUCT ID USE_STRUCT VARLIST ';'
    ;

SET_BASIC
    : {
        declaration_type=$<integer>0;
        $$=declaration_type;
      }
    ;

BEGIN_STRUCT
    : {
        $$=begin_struct($<text>0,yylineno);
      }
    ;

END_STRUCT
    : {
        $$=finish_struct($<context>-3);
      }
    ;

SET_STRUCT
    : {
        declaration_type=$<integer>0;
        $$=declaration_type;
      }
    ;

USE_STRUCT
    : {
        int type=find_struct_type($<text>0);
        if(type<0) compiler_error(yylineno,"Unknown structure %s",$<text>0);
        declaration_type=type<0 ? 0:type;
        $$=declaration_type;
      }
    ;

BASIC
    : INT { $$=0; }
    | LNG { $$=1; }
    | FLT { $$=2; }
    | DBL { $$=3; }
    ;

VARLIST
    : VARLIST ',' VAR {
        add_symbol($3->name,$3->type,current_table,@3.first_line);
        $$=$1;
      }
    | VAR {
        add_symbol($1->name,$1->type,current_table,@1.first_line);
        $$=0;
      }
    ;

VAR
    : ID DIM {
        $$=(variable_t*)malloc(sizeof(variable_t));
        $$->name=copy_text($1);
        $$->type=$2;
      }
    ;

DIM
    : '[' NUM ']' DIM {
        $$=add_array_type($4,(int)$2,@2.first_line);
      }
    | {
        $$=declaration_type;
      }
    ;

STMTLIST
    : {
        $$=new_statement(NULL);
      }
    | STMTLIST M STMT {
        backpatch($1->nextlist,$2);
        $$=new_statement($3->nextlist);
      }
    ;

STMT
    : ASGN {
        $$=new_statement(NULL);
      }
    | IF '(' BOOL ')' '{' M STMTLIST '}' ELSEPART {
        backpatch($3->truelist,$6);
        if($9->has_else){
            backpatch($3->falselist,$9->start);
            $$=new_statement(merge(merge($7->nextlist,$9->skip),$9->nextlist));
        }
        else $$=new_statement(merge($3->falselist,$7->nextlist));
      }
    | WHILE M '(' BOOL ')' M '{' STMTLIST '}' {
        backpatch($8->nextlist,$2);
        backpatch($4->truelist,$6);
        emit(@1.first_line,"JMP","none","-","-","-");
        set_label(instruction_count-1,$2);
        $$=new_statement($4->falselist);
      }
    ;

ELSEPART
    : {
        $$=(else_part_t*)malloc(sizeof(else_part_t));
        $$->has_else=0;
        $$->start=0;
        $$->skip=NULL;
        $$->nextlist=NULL;
      }
    | ELSE N '{' M STMTLIST '}' {
        $$=(else_part_t*)malloc(sizeof(else_part_t));
        $$->has_else=1;
        $$->skip=$2->nextlist;
        $$->start=$4;
        $$->nextlist=$5->nextlist;
      }
    ;

M
    : {
        $$=instruction_count;
      }
    ;

N
    : {
        int at=emit(yylineno,"JMP","none","-","-","-");
        $$=new_statement(makelist(at));
      }
    ;

BOOL
    : BOOL OR M BOOL {
        backpatch($1->falselist,$3);
        $$=new_boolean(merge($1->truelist,$4->truelist),$4->falselist);
      }
    | BOOL AND M BOOL {
        backpatch($1->truelist,$3);
        $$=new_boolean($4->truelist,merge($1->falselist,$4->falselist));
      }
    | NOT BOOL {
        $$=new_boolean($2->falselist,$2->truelist);
      }
    | '(' BOOL ')' {
        $$=new_boolean($2->truelist,$2->falselist);
      }
    | EXPR RELOP EXPR {
        int type=common_type($1->type,$3->type);
        expr_t *left=cast_value($1,type,@2.first_line);
        expr_t *right=cast_value($3,type,@2.first_line);
        const char *ops[]={"BR_EQ","BR_NE","BR_LT","BR_LE","BR_GT","BR_GE"};
        int yes=emit(@2.first_line,ops[$2],tac_type(type),"-",left->place,right->place);
        int no=emit(@2.first_line,"JMP","none","-","-","-");
        $$=new_boolean(makelist(yes),makelist(no));
      }
    ;

RELOP
    : EQ  { $$=0; }
    | NEQ { $$=1; }
    | '<' { $$=2; }
    | LEQ { $$=3; }
    | '>' { $$=4; }
    | GEQ { $$=5; }
    ;

ASGN
    : ITEM '=' EXPR ';' {
        if($1->type>3) compiler_error(@1.first_line,"Assignment needs a basic type on the left");
        else {
            expr_t *value=cast_value($3,$1->type,@2.first_line);
            char size[24];
            snprintf(size,sizeof(size),"%d",types[$1->type].size);
            emit(@2.first_line,"STORE",tac_type($1->type),$1->place,value->place,size);
        }
        $$=new_statement(NULL);
      }
    ;

EXPR
    : EXPR '+' TERM { $$=binary_value($1,$3,"ADD",@2.first_line); }
    | EXPR '-' TERM { $$=binary_value($1,$3,"SUB",@2.first_line); }
    | TERM { $$=$1; }
    ;

TERM
    : TERM '*' FACTOR { $$=binary_value($1,$3,"MUL",@2.first_line); }
    | TERM '/' FACTOR { $$=binary_value($1,$3,"DIV",@2.first_line); }
    | TERM '%' FACTOR { $$=binary_value($1,$3,"REM",@2.first_line); }
    | FACTOR { $$=$1; }
    ;

FACTOR
    : NUM {
        char text[TEXT_SIZE];
        snprintf(text,sizeof(text),"#%lld",$1);
        $$=new_expr(0,text);
      }
    | '-' NUM %prec NEG {
        char text[TEXT_SIZE];
        snprintf(text,sizeof(text),"#%lld",-$2);
        $$=new_expr(0,text);
      }
    | FLTCONST {
        char text[TEXT_SIZE];
        snprintf(text,sizeof(text),"#%.17g",$1);
        $$=new_expr(3,text);
      }
    | '-' FLTCONST %prec NEG {
        char text[TEXT_SIZE];
        snprintf(text,sizeof(text),"#%.17g",-$2);
        $$=new_expr(3,text);
      }
    | ITEM {
        $$=load_value($1,@1.first_line);
      }
    | '(' EXPR ')' {
        $$=$2;
      }
    ;

ITEM
    : ROOT SMPLITEM {
        $$=$2;
      }
    | ITEM '.' MEMBER_TABLE SMPLITEM {
        $$=member_address($1,$4,@2.first_line);
      }
    ;

ROOT
    : {
        $$=0;
      }
    ;

MEMBER_TABLE
    : {
        address_t *left=$<address>-1;
        if(left->type<0 || types[left->type].category!=STRUCT_TYPE){
            compiler_error(yylineno,"Member access needs a structure");
            $$=0;
        }
        else $$=types[left->type].table;
      }
    ;

SMPLITEM
    : ID {
        int table=$<integer>0;
        int at=find_symbol($1,table);
        if(at<0){
            compiler_error(@1.first_line,"Undeclared variable %s",$1);
            $$=new_address(0,"#0");
        }
        else {
            char text[TEXT_SIZE];
            snprintf(text,sizeof(text),"#%d",symbols[table][at].offset);
            $$=new_address(symbols[table][at].type,text);
        }
      }
    | AREF {
        $$=$1;
      }
    ;

AREF
    : AREF '[' EXPR ']' {
        $$=index_address($1,$3,@2.first_line);
      }
    | ID '[' EXPR ']' {
        int table=$<integer>0;
        int at=find_symbol($1,table);
        if(at<0){
            compiler_error(@1.first_line,"Undeclared variable %s",$1);
            $$=new_address(0,"#0");
        }
        else {
            char text[TEXT_SIZE];
            snprintf(text,sizeof(text),"#%d",symbols[table][at].offset);
            address_t *base=new_address(symbols[table][at].type,text);
            $$=index_address(base,$3,@2.first_line);
        }
      }
    ;

%%

char *copy_text(const char *text){
    char *answer=(char*)malloc(strlen(text)+1);
    if(answer) strcpy(answer,text);
    return answer;
}

void compiler_error(int line,const char *format,...){
    va_list args;
    fprintf(stderr,"Error at line %d: ",line);
    va_start(args,format);
    vfprintf(stderr,format,args);
    va_end(args);
    fprintf(stderr,"\n");
    had_error=1;
}

void lexer_error(const char *text){
    compiler_error(yylineno,"Unexpected character %s",text);
}

void yyerror(const char *text){
    compiler_error(yylineno,"%s",text);
}

int align_up(int value,int alignment){
    if(alignment<=1) return value;
    return ((value+alignment-1)/alignment)*alignment;
}

int type_alignment(int type){
    int alignment=types[type].align;
    if(alignment>max_alignment) alignment=max_alignment;
    return alignment;
}

int find_symbol(const char *name,int table){
    int i;
    if(table<0 || table>=table_count) return -1;
    for(i=0;i<symbol_count[table];i++){
        if(strcmp(symbols[table][i].name,name)==0) return i;
    }
    return -1;
}

void add_symbol(const char *name,int type,int table,int line){
    int alignment,offset;
    if(type<0 || type>=type_count) return;
    if(find_symbol(name,table)>=0){
        compiler_error(line,"Variable %s is already declared",name);
        return;
    }
    if(types[type].size<0){
        compiler_error(line,"Structure %s cannot contain itself directly",name);
        return;
    }
    if(symbol_count[table]>=MAX_SYMBOLS){
        compiler_error(line,"Too many symbols");
        return;
    }
    alignment=type_alignment(type);
    offset=align_up(table_size[table],alignment);
    strcpy(symbols[table][symbol_count[table]].name,name);
    symbols[table][symbol_count[table]].type=type;
    symbols[table][symbol_count[table]].offset=offset;
    symbol_count[table]++;
    table_size[table]=offset+types[type].size;
    if(alignment>table_align[table]) table_align[table]=alignment;
}

int add_array_type(int element,int count,int line){
    int i;
    char element_name[128];
    if(count<=0){
        compiler_error(line,"Array size must be positive");
        count=1;
    }
    for(i=4;i<type_count;i++){
        if(types[i].category==ARRAY_TYPE && types[i].element==element && types[i].count==count)
            return i;
    }
    if(type_count>=MAX_TYPES){
        compiler_error(line,"Too many types");
        return element;
    }
    i=type_count++;
    types[i].category=ARRAY_TYPE;
    types[i].element=element;
    types[i].count=count;
    types[i].table=-1;
    types[i].size=types[element].size*count;
    types[i].align=types[element].align;
    snprintf(element_name,sizeof(element_name),"%s",types[element].name);
    snprintf(types[i].name,sizeof(types[i].name),"array(%d,%.100s)",count,element_name);
    return i;
}

int find_struct_type(const char *name){
    int i;
    for(i=4;i<type_count;i++){
        if(types[i].category==STRUCT_TYPE && strcmp(types[i].tag,name)==0) return i;
    }
    return -1;
}

struct_context_t *begin_struct(const char *name,int line){
    struct_context_t *context=(struct_context_t*)malloc(sizeof(struct_context_t));
    int type;
    if(find_struct_type(name)>=0) compiler_error(line,"Structure %s is already defined",name);
    if(table_count>=MAX_TABLES || type_count>=MAX_TYPES){
        compiler_error(line,"Too many structures");
        context->old_table=current_table;
        context->table=0;
        context->type=0;
        return context;
    }
    context->old_table=current_table;
    context->table=table_count++;
    context->type=type_count++;
    snprintf(table_name[context->table],sizeof(table_name[context->table]),"struct %s",name);
    type=context->type;
    types[type].category=STRUCT_TYPE;
    types[type].element=-1;
    types[type].count=1;
    types[type].table=context->table;
    types[type].size=-1;
    types[type].align=1;
    snprintf(types[type].name,sizeof(types[type].name),"struct %s",name);
    snprintf(types[type].tag,sizeof(types[type].tag),"%s",name);
    current_table=context->table;
    return context;
}

int finish_struct(struct_context_t *context){
    int alignment=table_align[context->table];
    if(alignment<1) alignment=1;
    types[context->type].align=alignment;
    types[context->type].size=align_up(table_size[context->table],alignment);
    table_size[context->table]=types[context->type].size;
    current_table=context->old_table;
    return context->type;
}

const char *tac_type(int type){
    static const char *names[]={"i32","i64","f32","f64"};
    if(type>=0 && type<4) return names[type];
    return "none";
}

int emit(int line,const char *op,const char *type,const char *dst,const char *a1,const char *a2){
    instruction_t *ins;
    if(instruction_count>=MAX_INS){
        compiler_error(line,"Too many TAC instructions");
        return MAX_INS-1;
    }
    ins=&code[instruction_count];
    ins->line=line;
    snprintf(ins->op,sizeof(ins->op),"%s",op);
    snprintf(ins->type,sizeof(ins->type),"%s",type);
    snprintf(ins->dst,sizeof(ins->dst),"%s",dst);
    snprintf(ins->arg1,sizeof(ins->arg1),"%s",a1);
    snprintf(ins->arg2,sizeof(ins->arg2),"%s",a2);
    return instruction_count++;
}

void set_label(int instruction,int target){
    if(instruction<0 || instruction>=instruction_count) return;
    snprintf(code[instruction].dst,sizeof(code[instruction].dst),"L%d",target);
}

void new_temp(char *out){
    snprintf(out,TEXT_SIZE,"t%d",temporary_count++);
}

expr_t *new_expr(int type,const char *place){
    expr_t *value=(expr_t*)malloc(sizeof(expr_t));
    value->type=type;
    snprintf(value->place,sizeof(value->place),"%s",place);
    return value;
}

address_t *new_address(int type,const char *place){
    address_t *address=(address_t*)malloc(sizeof(address_t));
    address->type=type;
    snprintf(address->place,sizeof(address->place),"%s",place);
    return address;
}

statement_t *new_statement(list_node *list){
    statement_t *statement=(statement_t*)malloc(sizeof(statement_t));
    statement->nextlist=list;
    return statement;
}

boolean_t *new_boolean(list_node *true_list,list_node *false_list){
    boolean_t *value=(boolean_t*)malloc(sizeof(boolean_t));
    value->truelist=true_list;
    value->falselist=false_list;
    return value;
}

list_node *makelist(int value){
    list_node *node=(list_node*)malloc(sizeof(list_node));
    node->value=value;
    node->next=NULL;
    return node;
}

list_node *merge(list_node *first,list_node *second){
    list_node *now;
    if(first==NULL) return second;
    now=first;
    while(now->next) now=now->next;
    now->next=second;
    return first;
}

void backpatch(list_node *list,int target){
    while(list){
        set_label(list->value,target);
        list=list->next;
    }
}

int common_type(int first,int second){
    if(first==second) return first;
    if(first>3 || second>3){
        compiler_error(yylineno,"Arithmetic needs basic numeric types");
        return 0;
    }
    if(first==3 || second==3) return 3;
    if((first==1 && second==2) || (first==2 && second==1)) return 3;
    if(first==2 || second==2) return 2;
    return 1;
}

expr_t *cast_value(expr_t *value,int type,int line){
    static const char *short_name[]={"I32","I64","F32","F64"};
    char op[32],temp[TEXT_SIZE];
    if(value->type==type) return value;
    if(value->type>3 || type>3){
        compiler_error(line,"Invalid type conversion");
        return new_expr(type,"#0");
    }
    snprintf(op,sizeof(op),"CAST_%s_%s",short_name[value->type],short_name[type]);
    new_temp(temp);
    emit(line,op,tac_type(type),temp,value->place,"-");
    return new_expr(type,temp);
}

expr_t *binary_value(expr_t *left,expr_t *right,const char *op,int line){
    int type=common_type(left->type,right->type);
    char temp[TEXT_SIZE];
    if(strcmp(op,"REM")==0 && type>=2){
        compiler_error(line,"Remainder operator needs integer operands");
        return new_expr(0,"#0");
    }
    left=cast_value(left,type,line);
    right=cast_value(right,type,line);
    new_temp(temp);
    emit(line,op,tac_type(type),temp,left->place,right->place);
    return new_expr(type,temp);
}

expr_t *load_value(address_t *address,int line){
    char temp[TEXT_SIZE],size[24];
    if(address->type>3){
        compiler_error(line,"An array or structure cannot be used as a number");
        return new_expr(0,"#0");
    }
    new_temp(temp);
    snprintf(size,sizeof(size),"%d",types[address->type].size);
    emit(line,"LOAD",tac_type(address->type),temp,address->place,size);
    return new_expr(address->type,temp);
}

address_t *index_address(address_t *base,expr_t *index,int line){
    char multiply[TEXT_SIZE],address[TEXT_SIZE],size[24];
    int element;
    if(base->type<0 || types[base->type].category!=ARRAY_TYPE){
        compiler_error(line,"Indexing needs an array");
        return new_address(0,"#0");
    }
    index=cast_value(index,0,line);
    element=types[base->type].element;
    snprintf(size,sizeof(size),"#%d",types[element].size);
    new_temp(multiply);
    emit(line,"MUL","i32",multiply,size,index->place);
    new_temp(address);
    emit(line,"ADD","i32",address,base->place,multiply);
    return new_address(element,address);
}

address_t *member_address(address_t *base,address_t *member,int line){
    char result[TEXT_SIZE];
    if(base->type<0 || types[base->type].category!=STRUCT_TYPE){
        compiler_error(line,"Member access needs a structure");
        return new_address(0,"#0");
    }
    new_temp(result);
    emit(line,"ADD","i32",result,base->place,member->place);
    return new_address(member->type,result);
}

void initialize_types(void){
    const char *names[]={"int","long","float","double"};
    int sizes[]={4,8,4,8};
    int i;
    memset(types,0,sizeof(types));
    memset(symbol_count,0,sizeof(symbol_count));
    memset(table_size,0,sizeof(table_size));
    memset(table_align,0,sizeof(table_align));
    strcpy(table_name[0],"main");
    for(i=0;i<4;i++){
        types[i].category=BASIC_TYPE;
        types[i].element=i;
        types[i].count=1;
        types[i].table=-1;
        types[i].size=sizes[i];
        types[i].align=sizes[i];
        strcpy(types[i].name,names[i]);
    }
}

void print_usage(FILE *file,const char *program){
    fprintf(file,"Usage: %s <input.tc> [4|8]\n",program);
    fprintf(file,"       %s --help\n",program);
    fprintf(file,"\nMaximum alignment is optional and defaults to 8.\n");
}

int make_output_paths(const char *source){
    const char *base=source;
    const char *slash1=strrchr(source,'/');
    const char *slash2=strrchr(source,'\\');
    char name[256];
    char *dot;
    if(slash1 && slash1+1>base) base=slash1+1;
    if(slash2 && slash2+1>base) base=slash2+1;
    snprintf(name,sizeof(name),"%s",base);
    dot=strrchr(name,'.');
    if(dot && dot!=name) *dot='\0';
    if(name[0]=='\0') strcpy(name,"program");
    snprintf(output_directory,sizeof(output_directory),"output_%s",name);
    snprintf(info_path,sizeof(info_path),"%s/info.txt",output_directory);
    snprintf(tac_path,sizeof(tac_path),"%s/scalar_tac.txt",output_directory);
    return 0;
}

int write_info_file(void){
    FILE *file;
    int i,j;
    if(mkdir(output_directory,0777)!=0 && errno!=EEXIST){
        fprintf(stderr,"Could not create %s\n",output_directory);
        return 1;
    }
    file=fopen(info_path,"w");
    if(!file){
        fprintf(stderr,"Could not create %s\n",info_path);
        return 1;
    }
    fprintf(file,"TINY C COMPILER INFORMATION\n\n");
    fprintf(file,"Source file: %s\n",input_path);
    fprintf(file,"Maximum alignment: %d bytes\n\n",max_alignment);
    fprintf(file,"TYPE TABLE\n\n");
    fprintf(file,"ID     Size     Align    Category     Description\n");
    for(i=0;i<type_count;i++){
        const char *category="Basic";
        if(types[i].category==ARRAY_TYPE) category="Array";
        else if(types[i].category==STRUCT_TYPE) category="Structure";
        fprintf(file,"%-6d %-8d %-8d %-12s %s\n",i,types[i].size,type_alignment(i),category,types[i].name);
    }
    for(i=0;i<table_count;i++){
        fprintf(file,"\nSYMBOL TABLE %d: %s\n\n",i,table_name[i]);
        fprintf(file,"Name                     Offset     Size       Type\n");
        for(j=0;j<symbol_count[i];j++){
            int type=symbols[i][j].type;
            fprintf(file,"%-24s %-10d %-10d %s\n",symbols[i][j].name,symbols[i][j].offset,types[type].size,types[type].name);
        }
        fprintf(file,"\nTotal size: %d bytes\n",table_size[i]);
    }
    fclose(file);
    return 0;
}

int write_tac_file(void){
    FILE *file=fopen(tac_path,"w");
    int i;
    if(!file){
        fprintf(stderr,"Could not create %s\n",tac_path);
        return 1;
    }
    fprintf(file,"# TINYC_TAC_V1\n");
    fprintf(file,"# instruction|line|opcode|type|destination|argument1|argument2\n");
    for(i=0;i<instruction_count;i++){
        fprintf(file,"%d|%d|%s|%s|%s|%s|%s\n",i,code[i].line,code[i].op,code[i].type,code[i].dst,code[i].arg1,code[i].arg2);
    }
    fclose(file);
    return 0;
}

int main(int argc,char **argv){
    int result;
    if(argc==2 && (strcmp(argv[1],"--help")==0 || strcmp(argv[1],"-h")==0)){
        print_usage(stdout,argv[0]);
        return 0;
    }
    if(argc<2 || argc>3){
        print_usage(stderr,argv[0]);
        return 1;
    }
    if(argc==3){
        if(strcmp(argv[2],"4")!=0 && strcmp(argv[2],"8")!=0){
            fprintf(stderr,"Alignment must be 4 or 8.\n\n");
            print_usage(stderr,argv[0]);
            return 1;
        }
        max_alignment=atoi(argv[2]);
    }
    snprintf(input_path,sizeof(input_path),"%s",argv[1]);
    make_output_paths(input_path);
    yyin=fopen(input_path,"r");
    if(!yyin){
        fprintf(stderr,"Could not open %s\n",input_path);
        return 1;
    }
    initialize_types();
    result=yyparse();
    fclose(yyin);
    if(result!=0 || had_error){
        fprintf(stderr,"Compilation failed.\n");
        return 1;
    }
    if(write_info_file()!=0 || write_tac_file()!=0) return 1;
    printf("Compilation successful\n");
    printf("Information: %s\n",info_path);
    printf("TAC: %s\n",tac_path);
    return 0;
}
