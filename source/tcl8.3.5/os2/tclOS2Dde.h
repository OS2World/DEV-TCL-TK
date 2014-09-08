/*
 * tclOS2Dde.h --
 *
 *      This header file defines things for the OS/2 implementation of the
 *      DDEML library.
 *
 * Copyright (c) 2002 Illya Vaes
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 * SCCS: @(#) tclWinThrd.h 1.2 98/01/27 11:48:05
 */
 
#ifndef _TCLOS2DDE
#define _TCLOS2DDE

/* Data types, conversion to OS/2 types */
#define CALLBACK	APIENTRY
#define DWORD		LONG
typedef struct _CONV {
    CONVCONTEXT	context;
} CONV, *PCONV;
#define BOOL16		DWORD
#define HANDLE		LHANDLE
#define HCONV		PCONV
#define HDDEDATA	PVOID
#define HSZ		PSZ
#define LPBYTE		PBYTE
#define LPDWORD		PLONG
#define LPSTR		PSZ
#define LPCSTR		PSZ
#define LPWSTR		PSZ /* twice the length of equivalent of LPSTR */
#define LPCWSTR		PSZ /* twice the length of equivalent of LPCSTR */

#define ERR printf

/* Structure for XTYP_WILDCONNECT */
typedef struct tagHSZPAIR {
    HSZ hszSvc;
    HSZ hszTopic;
} HSZPAIR;
typedef HSZPAIR *PHSZPAIR;

/* Structure for DdeConnect, DdeConnectList, XTYP_CONNECT, XTYP_WILDCONNECT */
/* CONVCONTEXT and PCONVCONTEXT defined in PM */

/* Structure for HCONVLIST */
typedef HCONV *HCONVLIST;

/* Structure for DdeQueryConvInfo */
typedef struct tagCONVINFO {
    DWORD cb;		/* sizeof(CONVINFO)  */
    DWORD hUser;	/* user specified field  */
    HCONV hConvPartner;	/* hConv on other end or 0 if non-ddemgr partner  */
    HSZ hszSvcPartner;	/* app name of partner if obtainable  */
    HSZ hszServiceReq;	/* AppName requested for connection  */
    HSZ hszTopic;	/* Topic name for conversation  */
    HSZ hszItem;	/* transaction item name or NULL if quiescent  */
    UINT wFmt;		/* transaction format or NULL if quiescent  */
    UINT wType;		/* XTYP_ for current transaction  */
    UINT wStatus;	/* ST_ constant for current conversation  */
    UINT wConvst;	/* XST_ constant for current transaction  */
    UINT wLastError;	/* last transaction error.  */
    HCONVLIST hConvList; /* parent hConvList if this conversation in a list */
    CONVCONTEXT ConvCtxt; /* conversation context */
    HWND hwnd;		/* window handle for this conversation */
    HWND hwndPartner;	/* partner window handle for this conversation */
} CONVINFO;
typedef CONVINFO *PCONVINFO;

/* Conversation states */
/* quiescent states */
#define XST_NULL              0
#define XST_INCOMPLETE        1
#define XST_CONNECTED         2
/* mid-initiation states */
#define XST_INIT1             3
#define XST_INIT2             4
/* active conversation states */
#define XST_REQSENT           5
#define XST_DATARCVD          6
#define XST_POKESENT          7
#define XST_POKEACKRCVD       8
#define XST_EXECSENT          9
#define XST_EXECACKRCVD      10
#define XST_ADVSENT          11
#define XST_UNADVSENT        12
#define XST_ADVACKRCVD       13
#define XST_UNADVACKRCVD     14
#define XST_ADVDATASENT      15
#define XST_ADVDATAACKRCVD   16

/* Constant used in the low word of dwData1 of XTYP_ADVREQ */
#define CADV_LATEACK         0xFFFF

/* Connection status bits */
#define ST_CONNECTED            0x0001
#define ST_ADVISE               0x0002
#define ST_ISLOCAL              0x0004
#define ST_BLOCKED              0x0008
#define ST_CLIENT               0x0010
#define ST_TERMINATED           0x0020
#define ST_INLIST               0x0040
#define ST_BLOCKNEXT            0x0080
#define ST_ISSELF               0x0100

/* Message filter hook type */
#define MSGF_DDEMGR             0x8001

/* Codepages */
#define CP_WINANSI	1004 /* default for Windows and old DDE */
#define CP_WINUNICODE	1200
#ifdef UNICODE
#define CP_WINNEUTRAL   CP_WINUNICODE
#else
#define CP_WINNEUTRAL   CP_WINANSI
#endif

/* Transactions */
#define XTYPF_NOBLOCK            0x0002  /* CBR_BLOCK will not work */
#define XTYPF_NODATA             0x0004  /* DDE_FDEFERUPD */
#define XTYPF_ACKREQ             0x0008  /* DDE_FACKREQ */

#define XCLASS_MASK              0xFC00
#define XCLASS_BOOL              0x1000
#define XCLASS_DATA              0x2000
#define XCLASS_FLAGS             0x4000
#define XCLASS_NOTIFICATION      0x8000

#define XTYP_ERROR              (0x0000 | XCLASS_NOTIFICATION | XTYPF_NOBLOCK )
#define XTYP_ADVDATA            (0x0010 | XCLASS_FLAGS         )
#define XTYP_ADVREQ             (0x0020 | XCLASS_DATA | XTYPF_NOBLOCK )
#define XTYP_ADVSTART           (0x0030 | XCLASS_BOOL          )
#define XTYP_ADVSTOP            (0x0040 | XCLASS_NOTIFICATION)
#define XTYP_EXECUTE            (0x0050 | XCLASS_FLAGS         )
#define XTYP_CONNECT            (0x0060 | XCLASS_BOOL | XTYPF_NOBLOCK)
#define XTYP_CONNECT_CONFIRM    (0x0070 | XCLASS_NOTIFICATION | XTYPF_NOBLOCK)
#define XTYP_XACT_COMPLETE      (0x0080 | XCLASS_NOTIFICATION  )
#define XTYP_POKE               (0x0090 | XCLASS_FLAGS         )
#define XTYP_REGISTER           (0x00A0 | XCLASS_NOTIFICATION | XTYPF_NOBLOCK)
#define XTYP_REQUEST            (0x00B0 | XCLASS_DATA          )
#define XTYP_DISCONNECT         (0x00C0 | XCLASS_NOTIFICATION | XTYPF_NOBLOCK)
#define XTYP_UNREGISTER         (0x00D0 | XCLASS_NOTIFICATION | XTYPF_NOBLOCK)
#define XTYP_WILDCONNECT        (0x00E0 | XCLASS_DATA | XTYPF_NOBLOCK)

#define XTYP_MASK                0x00F0
#define XTYP_SHIFT               4  /* shift to turn XTYP_ into an index */

/* Transaction IDs */
#define QID_SYNC                0xFFFFFFFF

/* Timeouts */
#define TIMEOUT_ASYNC           0xFFFFFFFF

/* Public strings used in DDE not defined in OS/2 */
#define SZDDE_ITEM_ITEMLIST    "TopicItemList"

/* Function prototypes */
typedef HDDEDATA APIENTRY FNCALLBACK(UINT wType, UINT wFmt, HCONV hConv,
                                     HSZ hsz1, HSZ hsz2, HDDEDATA hData,
                                     DWORD dwData1, DWORD dwData2);
typedef FNCALLBACK *PFNCALLBACK;
#define     CBR_BLOCK           ((HDDEDATA)0xffffffffL)
/* DLL registration functions */
UINT DdeInitializeA(LPDWORD pidInst, PFNCALLBACK pfnCallback, DWORD afCmd,
                    DWORD ulRes);
UINT DdeInitializeW(LPDWORD pidInst, PFNCALLBACK pfnCallback, DWORD afCmd,
                    DWORD ulRes);
#ifdef UNICODE
#define DdeInitialize  DdeInitializeW /* Wide */
#else
#define DdeInitialize  DdeInitializeA /* ASCII */
#endif
BOOL DdeUninitialize(DWORD idInst);

/* conversation enumeration functions */
HCONVLIST DdeConnectList(DWORD idInst, HSZ hszService, HSZ hszTopic,
                         HCONVLIST hConvList, PCONVCONTEXT pCC);
HCONV DdeQueryNextServer(HCONVLIST hConvList, HCONV hConvPrev);
BOOL DdeDisconnectList(HCONVLIST hConvList);

/* conversation control functions */
HCONV DdeConnect(DWORD idInst, HSZ hszService, HSZ hszTopic, PCONVCONTEXT pCC);
BOOL DdeDisconnect(HCONV hConv);
HCONV DdeReconnect(HCONV hConv);
UINT DdeQueryConvInfo(HCONV hConv, DWORD idTransaction, PCONVINFO pConvInfo);
BOOL DdeSetUserHandle(HCONV hConv, DWORD id, DWORD hUser);
BOOL DdeAbandonTransaction(DWORD idInst, HCONV hConv, DWORD idTransaction);

/* app server interface functions */
BOOL DdePostAdvise(DWORD idInst, HSZ hszTopic, HSZ hszItem);
BOOL DdeEnableCallback(DWORD idInst, HCONV hConv, UINT wCmd);
BOOL DdeImpersonateClient(HCONV hConv);
HDDEDATA DdeNameService(DWORD idInst, HSZ hsz1, HSZ hsz2, UINT afCmd);

/* app client interface functions */
HDDEDATA DdeClientTransaction(LPBYTE pData, DWORD cbData, HCONV hConv,
                              HSZ hszItem, UINT wFmt, UINT wType,
                              DWORD dwTimeout, LPDWORD pdwResult);

/* data transfer functions */
HDDEDATA DdeCreateDataHandle(DWORD idInst, LPBYTE pSrc, DWORD cb, DWORD cbOff,
                             HSZ hszItem, UINT wFmt, UINT afCmd);
HDDEDATA DdeAddData(HDDEDATA hData, LPBYTE pSrc, DWORD cb, DWORD cbOff);
DWORD DdeGetData(HDDEDATA hData, LPBYTE pDst, DWORD cbMax, DWORD cbOff);
LPBYTE DdeAccessData(HDDEDATA hData, LPDWORD pcbDataSize);
BOOL DdeUnaccessData(HDDEDATA hData);
BOOL DdeFreeDataHandle(HDDEDATA hData);
UINT DdeGetLastError(DWORD idInst);
HSZ  DdeCreateStringHandleA(DWORD idInst, LPCSTR psz, int iCodePage);
HSZ  DdeCreateStringHandleW(DWORD idInst, LPCWSTR psz, int iCodePage);
#ifdef UNICODE
#define DdeCreateStringHandle  DdeCreateStringHandleW /* Wide */
#else
#define DdeCreateStringHandle  DdeCreateStringHandleA /* ASCII */
#endif
DWORD DdeQueryStringA(DWORD idInst, HSZ hsz, LPSTR psz, DWORD cchMax,
                      int iCodePage);
DWORD DdeQueryStringW(DWORD idInst, HSZ hsz, LPWSTR psz, DWORD cchMax,
                      int iCodePage);
#ifdef UNICODE
#define DdeQueryString  DdeQueryStringW /* Wide */
#else
#define DdeQueryString  DdeQueryStringA /* ASCII */
#endif
BOOL DdeFreeStringHandle(DWORD idInst, HSZ hsz);
BOOL DdeKeepStringHandle(DWORD idInst, HSZ hsz);
int DdeCmpStringHandles(HSZ hsz1, HSZ hsz2);

/* Filter flags */
#define CBF_FAIL_SELFCONNECTIONS     0x00001000
#define CBF_FAIL_CONNECTIONS         0x00002000
#define CBF_FAIL_ADVISES             0x00004000
#define CBF_FAIL_EXECUTES            0x00008000
#define CBF_FAIL_POKES               0x00010000
#define CBF_FAIL_REQUESTS            0x00020000
#define CBF_FAIL_ALLSVRXACTIONS      0x0003f000
#define CBF_SKIP_CONNECT_CONFIRMS    0x00040000
#define CBF_SKIP_REGISTRATIONS       0x00080000
#define CBF_SKIP_UNREGISTRATIONS     0x00100000
#define CBF_SKIP_DISCONNECTS         0x00200000
#define CBF_SKIP_ALLNOTIFICATIONS    0x003c0000

/* Application command flags */
#define     APPCMD_CLIENTONLY            0x00000010L
#define     APPCMD_FILTERINITS           0x00000020L
#define     APPCMD_MASK                  0x00000FF0L

/* Application classification flags */
#define     APPCLASS_STANDARD            0x00000000L
#define     APPCLASS_MASK                0x0000000FL

/* Enable Callback flags */
#define EC_ENABLEALL            0
#define EC_ENABLEONE            ST_BLOCKNEXT
#define EC_DISABLE              ST_BLOCKED
#define EC_QUERYWAITING         2

/* DDE Name Service flags */
#define DNS_REGISTER        0x0001
#define DNS_UNREGISTER      0x0002
#define DNS_FILTERON        0x0004
#define DNS_FILTEROFF       0x0008

/* Special data handle constants */
#define HDATA_APPOWNED          0x0001

/* Error values */
#define     DMLERR_NO_ERROR                    0       /* must be 0 */
#define     DMLERR_FIRST                       0x4000

#define     DMLERR_ADVACKTIMEOUT               0x4000
#define     DMLERR_BUSY                        0x4001
#define     DMLERR_DATAACKTIMEOUT              0x4002
#define     DMLERR_DLL_NOT_INITIALIZED         0x4003
#define     DMLERR_DLL_USAGE                   0x4004
#define     DMLERR_EXECACKTIMEOUT              0x4005
#define     DMLERR_INVALIDPARAMETER            0x4006
#define     DMLERR_LOW_MEMORY                  0x4007
#define     DMLERR_MEMORY_ERROR                0x4008
#define     DMLERR_NOTPROCESSED                0x4009
#define     DMLERR_NO_CONV_ESTABLISHED         0x400a
#define     DMLERR_POKEACKTIMEOUT              0x400b
#define     DMLERR_POSTMSG_FAILED              0x400c
#define     DMLERR_REENTRANCY                  0x400d
#define     DMLERR_SERVER_DIED                 0x400e
#define     DMLERR_SYS_ERROR                   0x400f
#define     DMLERR_UNADVACKTIMEOUT             0x4010
#define     DMLERR_UNFOUND_QUEUE_ID            0x4011

#define     DMLERR_LAST                        0x4011

/* No DDEML spying */

#endif /* _TCLOS2DDE */
