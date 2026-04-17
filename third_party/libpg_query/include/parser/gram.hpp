/* A Bison parser, made by GNU Bison 3.0.4.  */

/* Bison interface for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015 Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

#ifndef YY_BASE_YY_GRAMMAR_GRAMMAR_OUT_HPP_INCLUDED
# define YY_BASE_YY_GRAMMAR_GRAMMAR_OUT_HPP_INCLUDED
/* Debug traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif
#if YYDEBUG
extern int base_yydebug;
#endif

/* Token type.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
  enum yytokentype
  {
    IDENT = 258,
    FCONST = 259,
    SCONST = 260,
    BCONST = 261,
    XCONST = 262,
    Op = 263,
    ICONST = 264,
    PARAM = 265,
    TYPECAST = 266,
    DOT_DOT = 267,
    COLON_EQUALS = 268,
    EQUALS_GREATER = 269,
    INTEGER_DIVISION = 270,
    POWER_OF = 271,
    SINGLE_ARROW = 272,
    DOUBLE_ARROW = 273,
    SHIFT_RIGHT = 274,
    SHIFT_LEFT = 275,
    LESS_EQUALS = 276,
    GREATER_EQUALS = 277,
    NOT_EQUALS = 278,
    NOT_DISTINCT_FROM = 279,
    ABORT_P = 280,
    ABSOLUTE_P = 281,
    ACCESS = 282,
    ACTION = 283,
    ADD_P = 284,
    ADMIN = 285,
    AFTER = 286,
    AGGREGATE = 287,
    ALL = 288,
    ALSO = 289,
    ALTER = 290,
    ALWAYS = 291,
    ANALYSE = 292,
    ANALYZE = 293,
    AND = 294,
    ANTI = 295,
    ANY = 296,
    ARRAY = 297,
    AS = 298,
    ASC_P = 299,
    ASOF = 300,
    ASSERTION = 301,
    ASSIGNMENT = 302,
    ASYMMETRIC = 303,
    AT = 304,
    ATTACH = 305,
    ATTRIBUTE = 306,
    AUTHORIZATION = 307,
    BACKWARD = 308,
    BEFORE = 309,
    BEGIN_P = 310,
    BETWEEN = 311,
    BIGINT = 312,
    BINARY = 313,
    BIT = 314,
    BOOLEAN_P = 315,
    BOTH = 316,
    BY = 317,
    CACHE = 318,
    CALL_P = 319,
    CALLED = 320,
    CASCADE = 321,
    CASCADED = 322,
    CASE = 323,
    CAST = 324,
    CATALOG_P = 325,
    CENTURIES_P = 326,
    CENTURY_P = 327,
    CHAIN = 328,
    CHAR_P = 329,
    CHARACTER = 330,
    CHARACTERISTICS = 331,
    CHECK_P = 332,
    CHECKPOINT = 333,
    CLASS = 334,
    CLOSE = 335,
    CLUSTER = 336,
    COALESCE = 337,
    COLLATE = 338,
    COLLATION = 339,
    COLUMN = 340,
    COLUMNS = 341,
    COMMENT = 342,
    COMMENTS = 343,
    COMMIT = 344,
    COMMITTED = 345,
    COMPRESSION = 346,
    CONCURRENTLY = 347,
    CONFIGURATION = 348,
    CONFLICT = 349,
    CONNECTION = 350,
    CONSTRAINT = 351,
    CONSTRAINTS = 352,
    CONTENT_P = 353,
    CONTINUE_P = 354,
    CONVERSION_P = 355,
    CONVERT = 356,
    COPY = 357,
    COST = 358,
    CREATE_P = 359,
    CROSS = 360,
    CSV = 361,
    CUBE = 362,
    CURRENT_P = 363,
    CURSOR = 364,
    CYCLE = 365,
    DATA_P = 366,
    DATABASE = 367,
    DAY_P = 368,
    DAYS_P = 369,
    DEALLOCATE = 370,
    DEC = 371,
    DECADE_P = 372,
    DECADES_P = 373,
    DECIMAL_P = 374,
    DECLARE = 375,
    DEFAULT = 376,
    DEFAULTS = 377,
    DEFERRABLE = 378,
    DEFERRED = 379,
    DEFINER = 380,
    DELETE_P = 381,
    DELIMITER = 382,
    DELIMITERS = 383,
    DEPENDS = 384,
    DESC_P = 385,
    DESCRIBE = 386,
    DETACH = 387,
    DICTIONARY = 388,
    DISABLE_P = 389,
    DISCARD = 390,
    DISTINCT = 391,
    DISTINCTROW = 392,
    DIV = 393,
    DO = 394,
    DOCUMENT_P = 395,
    DOMAIN_P = 396,
    DOUBLE_P = 397,
    DROP = 398,
    EACH = 399,
    ELSE = 400,
    ENABLE_P = 401,
    ENCODING = 402,
    ENCRYPTED = 403,
    END_P = 404,
    ENUM_P = 405,
    ERROR_P = 406,
    ESCAPE = 407,
    EVENT = 408,
    EXCEPT = 409,
    EXCLUDE = 410,
    EXCLUDING = 411,
    EXCLUSIVE = 412,
    EXECUTE = 413,
    EXISTS = 414,
    EXPLAIN = 415,
    EXPORT_P = 416,
    EXPORT_STATE = 417,
    EXTENSION = 418,
    EXTENSIONS = 419,
    EXTERNAL = 420,
    EXTRACT = 421,
    FALSE_P = 422,
    FAMILY = 423,
    FETCH = 424,
    FILTER = 425,
    FIRST_P = 426,
    FLOAT_P = 427,
    FOLLOWING = 428,
    FOR = 429,
    FORCE = 430,
    FOREIGN = 431,
    FORWARD = 432,
    FREEZE = 433,
    FROM = 434,
    FULL = 435,
    FUNCTION = 436,
    FUNCTIONS = 437,
    GENERATED = 438,
    GLOB = 439,
    GLOBAL = 440,
    GRANT = 441,
    GRANTED = 442,
    GROUP_P = 443,
    GROUPING = 444,
    GROUPING_ID = 445,
    GROUPS = 446,
    HANDLER = 447,
    HAVING = 448,
    HEADER_P = 449,
    HIGH_PRIORITY = 450,
    HOLD = 451,
    HOUR_P = 452,
    HOURS_P = 453,
    IDENTITY_P = 454,
    IF_P = 455,
    IGNORE_P = 456,
    ILIKE = 457,
    IMMEDIATE = 458,
    IMMUTABLE = 459,
    IMPLICIT_P = 460,
    IMPORT_P = 461,
    IN_P = 462,
    INCLUDE_P = 463,
    INCLUDING = 464,
    INCREMENT = 465,
    INDEX = 466,
    INDEXES = 467,
    INHERIT = 468,
    INHERITS = 469,
    INITIALLY = 470,
    INLINE_P = 471,
    INNER_P = 472,
    INOUT = 473,
    INPUT_P = 474,
    INSENSITIVE = 475,
    INSERT = 476,
    INSTALL = 477,
    INSTEAD = 478,
    INT_P = 479,
    INTEGER = 480,
    INTERSECT = 481,
    INTERVAL = 482,
    INTO = 483,
    INVOKER = 484,
    IS = 485,
    ISNULL = 486,
    ISOLATION = 487,
    JOIN = 488,
    JSON = 489,
    KEY = 490,
    LABEL = 491,
    LAMBDA = 492,
    LANGUAGE = 493,
    LARGE_P = 494,
    LAST_P = 495,
    LATERAL_P = 496,
    LEADING = 497,
    LEAKPROOF = 498,
    LEFT = 499,
    LEVEL = 500,
    LIKE = 501,
    LIMIT = 502,
    LISTEN = 503,
    LOAD = 504,
    LOCAL = 505,
    LOCATION = 506,
    LOCK_P = 507,
    LOCKED = 508,
    LOGGED = 509,
    MACRO = 510,
    MAP = 511,
    MAPPING = 512,
    MATCH = 513,
    MATCHED = 514,
    MATERIALIZED = 515,
    MAXVALUE = 516,
    MERGE = 517,
    METHOD = 518,
    MICROSECOND_P = 519,
    MICROSECONDS_P = 520,
    MILLENNIA_P = 521,
    MILLENNIUM_P = 522,
    MILLISECOND_P = 523,
    MILLISECONDS_P = 524,
    MINUTE_P = 525,
    MINUTES_P = 526,
    MINVALUE = 527,
    MOD = 528,
    MODE = 529,
    MONTH_P = 530,
    MONTHS_P = 531,
    MOVE = 532,
    NAME_P = 533,
    NAMES = 534,
    NATIONAL = 535,
    NATURAL = 536,
    NCHAR = 537,
    NEW = 538,
    NEXT = 539,
    NO = 540,
    NONE = 541,
    NOT = 542,
    NOTHING = 543,
    NOTIFY = 544,
    NOTNULL = 545,
    NOWAIT = 546,
    NULL_P = 547,
    NULLIF = 548,
    NULLS_P = 549,
    NUMERIC = 550,
    OBJECT_P = 551,
    OF = 552,
    OFF = 553,
    OFFSET = 554,
    OIDS = 555,
    OLD = 556,
    ON = 557,
    ONLY = 558,
    OPERATOR = 559,
    OPTION = 560,
    OPTIONS = 561,
    OR = 562,
    ORDER = 563,
    ORDINALITY = 564,
    OTHERS = 565,
    OUT_P = 566,
    OUTER_P = 567,
    OVER = 568,
    OVERLAPS = 569,
    OVERLAY = 570,
    OVERRIDING = 571,
    OWNED = 572,
    OWNER = 573,
    PARALLEL = 574,
    PARSER = 575,
    PARTIAL = 576,
    PARTITION = 577,
    PARTITIONED = 578,
    PASSING = 579,
    PASSWORD = 580,
    PERCENT = 581,
    PERSISTENT = 582,
    PIVOT = 583,
    PIVOT_LONGER = 584,
    PIVOT_WIDER = 585,
    PLACING = 586,
    PLANS = 587,
    POLICY = 588,
    POSITION = 589,
    POSITIONAL = 590,
    PRAGMA_P = 591,
    PRECEDING = 592,
    PRECISION = 593,
    PREPARE = 594,
    PREPARED = 595,
    PRESERVE = 596,
    PRIMARY = 597,
    PRIOR = 598,
    PRIVILEGES = 599,
    PROCEDURAL = 600,
    PROCEDURE = 601,
    PROGRAM = 602,
    PUBLICATION = 603,
    QUALIFY = 604,
    QUARTER_P = 605,
    QUARTERS_P = 606,
    QUOTE = 607,
    RANGE = 608,
    READ_P = 609,
    REAL = 610,
    REASSIGN = 611,
    RECHECK = 612,
    RECURSIVE = 613,
    REF = 614,
    REFERENCES = 615,
    REFERENCING = 616,
    REFRESH = 617,
    REGEXP = 618,
    REINDEX = 619,
    RELATIVE_P = 620,
    RELEASE = 621,
    RENAME = 622,
    REPEATABLE = 623,
    REPLACE = 624,
    REPLICA = 625,
    RESET = 626,
    RESPECT_P = 627,
    RESTART = 628,
    RESTRICT = 629,
    RETURNING = 630,
    RETURNS = 631,
    REVOKE = 632,
    RIGHT = 633,
    RLIKE = 634,
    ROLE = 635,
    ROLLBACK = 636,
    ROLLUP = 637,
    ROW = 638,
    ROWS = 639,
    RULE = 640,
    SAMPLE = 641,
    SAVEPOINT = 642,
    SCHEMA = 643,
    SCHEMAS = 644,
    SCOPE = 645,
    SCROLL = 646,
    SEARCH = 647,
    SECOND_P = 648,
    SECONDS_P = 649,
    SECRET = 650,
    SECURITY = 651,
    SELECT = 652,
    SEMI = 653,
    SEQUENCE = 654,
    SEQUENCES = 655,
    SERIALIZABLE = 656,
    SERVER = 657,
    SESSION = 658,
    SET = 659,
    SETOF = 660,
    SETS = 661,
    SHARE = 662,
    SHOW = 663,
    SIMILAR = 664,
    SIMPLE = 665,
    SKIP = 666,
    SMALLINT = 667,
    SNAPSHOT = 668,
    SOME = 669,
    SORTED = 670,
    SOURCE_P = 671,
    SQL_BIG_RESULT = 672,
    SQL_BUFFER_RESULT = 673,
    SQL_CACHE = 674,
    SQL_CALC_FOUND_ROWS = 675,
    SQL_NO_CACHE = 676,
    SQL_P = 677,
    SQL_SMALL_RESULT = 678,
    STABLE = 679,
    STANDALONE_P = 680,
    START = 681,
    STATEMENT = 682,
    STATISTICS = 683,
    STDIN = 684,
    STDOUT = 685,
    STORAGE = 686,
    STORED = 687,
    STRAIGHT_JOIN = 688,
    STRICT_P = 689,
    STRIP_P = 690,
    STRUCT = 691,
    SUBSCRIPTION = 692,
    SUBSTR = 693,
    SUBSTRING = 694,
    SUMMARIZE = 695,
    SYMMETRIC = 696,
    SYSID = 697,
    SYSTEM_P = 698,
    TABLE = 699,
    TABLES = 700,
    TABLESAMPLE = 701,
    TABLESPACE = 702,
    TARGET_P = 703,
    TEMP = 704,
    TEMPLATE = 705,
    TEMPORARY = 706,
    TEXT_P = 707,
    THEN = 708,
    TIES = 709,
    TIME = 710,
    TIMESTAMP = 711,
    TIMESTAMPADD = 712,
    TIMESTAMPDIFF = 713,
    TO = 714,
    TRAILING = 715,
    TRANSACTION = 716,
    TRANSFORM = 717,
    TREAT = 718,
    TRIGGER = 719,
    TRIM = 720,
    TRUE_P = 721,
    TRUNCATE = 722,
    TRUSTED = 723,
    TRY_CAST = 724,
    TYPE_P = 725,
    TYPES_P = 726,
    UNBOUNDED = 727,
    UNCOMMITTED = 728,
    UNENCRYPTED = 729,
    UNION = 730,
    UNIQUE = 731,
    UNKNOWN = 732,
    UNLISTEN = 733,
    UNLOGGED = 734,
    UNPACK = 735,
    UNPIVOT = 736,
    UNTIL = 737,
    UPDATE = 738,
    USE_P = 739,
    USER = 740,
    USING = 741,
    VACUUM = 742,
    VALID = 743,
    VALIDATE = 744,
    VALIDATOR = 745,
    VALUE_P = 746,
    VALUES = 747,
    VARCHAR = 748,
    VARIABLE_P = 749,
    VARIADIC = 750,
    VARYING = 751,
    VERBOSE = 752,
    VERSION_P = 753,
    VIEW = 754,
    VIEWS = 755,
    VIRTUAL = 756,
    VOLATILE = 757,
    WEEK_P = 758,
    WEEKS_P = 759,
    WHEN = 760,
    WHERE = 761,
    WHITESPACE_P = 762,
    WINDOW = 763,
    WITH = 764,
    WITHIN = 765,
    WITHOUT = 766,
    WORK = 767,
    WRAPPER = 768,
    WRITE_P = 769,
    XML_P = 770,
    XMLATTRIBUTES = 771,
    XMLCONCAT = 772,
    XMLELEMENT = 773,
    XMLEXISTS = 774,
    XMLFOREST = 775,
    XMLNAMESPACES = 776,
    XMLPARSE = 777,
    XMLPI = 778,
    XMLROOT = 779,
    XMLSERIALIZE = 780,
    XMLTABLE = 781,
    YEAR_P = 782,
    YEARS_P = 783,
    YES_P = 784,
    ZONE = 785,
    NOT_LA = 786,
    NULLS_LA = 787,
    WITH_LA = 788,
    SINGLE_COLON = 789,
    CONDITIONLESS_JOIN = 790,
    POSTFIXOP = 791,
    UMINUS = 792
  };
#endif

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED

union YYSTYPE
{
#line 14 "third_party/libpg_query/grammar/grammar.y" /* yacc.c:1909  */

	core_YYSTYPE		core_yystype;
	/* these fields must match core_YYSTYPE: */
	int					ival;
	char				*str;
	const char			*keyword;
	const char          *conststr;

	char				chr;
	bool				boolean;
	PGJoinType			jtype;
	PGDropBehavior		dbehavior;
	PGOnCommitAction		oncommit;
	PGOnCreateConflict		oncreateconflict;
	PGList				*list;
	PGNode				*node;
	PGValue				*value;
	PGObjectType			objtype;
	PGTypeName			*typnam;
	PGObjectWithArgs		*objwithargs;
	PGDefElem				*defelt;
	PGSortBy				*sortby;
	PGWindowDef			*windef;
	PGJoinExpr			*jexpr;
	PGIndexElem			*ielem;
	PGAlias				*alias;
	PGRangeVar			*range;
	PGIntoClause			*into;
	PGCTEMaterialize			ctematerialize;
	PGWithClause			*with;
	PGInferClause			*infer;
	PGOnConflictClause	*onconflict;
	PGOnConflictActionAlias onconflictshorthand;
	PGAIndices			*aind;
	PGResTarget			*target;
	PGInsertStmt			*istmt;
	PGVariableSetStmt		*vsetstmt;
	PGOverridingKind       override;
	PGSortByDir            sortorder;
	PGSortByNulls          nullorder;
	PGIgnoreNulls          ignorenulls;
	PGConstrType           constr;
	PGLockClauseStrength lockstrength;
	PGLockWaitPolicy lockwaitpolicy;
	PGSubLinkType subquerytype;
	PGViewCheckOption viewcheckoption;
	PGInsertColumnOrder bynameorposition;
	PGLoadInstallType loadinstalltype;
	PGTransactionStmtType transactiontype;
	PGMergeAction mergeaction;

#line 644 "grammar/grammar_out.hpp" /* yacc.c:1909  */
};

typedef union YYSTYPE YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define YYSTYPE_IS_DECLARED 1
#endif

/* Location type.  */
#if ! defined YYLTYPE && ! defined YYLTYPE_IS_DECLARED
typedef struct YYLTYPE YYLTYPE;
struct YYLTYPE
{
  int first_line;
  int first_column;
  int last_line;
  int last_column;
};
# define YYLTYPE_IS_DECLARED 1
# define YYLTYPE_IS_TRIVIAL 1
#endif



int base_yyparse (core_yyscan_t yyscanner);

#endif /* !YY_BASE_YY_GRAMMAR_GRAMMAR_OUT_HPP_INCLUDED  */
