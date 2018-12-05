
#include "tls_smtp_comm.h"

//#define WIN32
#ifndef WIN32

tls_smtp_comm::tls_smtp_comm(int p, const std::string &s) { }
tls_smtp_comm::~tls_smtp_comm() { }
int tls_smtp_comm::command(const std::string &cmd) { return -1; }
int tls_smtp_comm::send(const std::string &data) { return -1; }

#else

// from TLSclient.c - SSPI Schannel gmail TLS connection example

#define SECURITY_WIN32
#define IO_BUFFER_SIZE  0x10000
#define DLL_NAME TEXT("Secur32.dll")
#define NT4_DLL_NAME TEXT("Security.dll")

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <winsock.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <schannel.h>
#include <security.h>
#include <sspi.h>

#include "log_sched.h"

#pragma comment(lib, "WSock32.Lib")
#pragma comment(lib, "Crypt32.Lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "MSVCRTD.lib")

// Globals.
DWORD   dwProtocol      = SP_PROT_TLS1; // SP_PROT_TLS1; // SP_PROT_PCT1; SP_PROT_SSL2; SP_PROT_SSL3; 0=default
ALG_ID  aiKeyExch       = 0; // = default; CALG_DH_EPHEM; CALG_RSA_KEYX;
/*
BOOL    fVerbose        = FALSE; // FALSE; // TRUE;


INT     iPortNumber     = 465; // gmail TLS
//LPSTR   pszServerName   = "smtp.gmail.com"; // DNS name of server
LPSTR   pszServerName   = "correo.sarenet.es"; // DNS name of server
LPSTR   pszUser         = 0; // if specified, a certificate in "MY" store is searched for

BOOL    fUseProxy       = FALSE;
LPSTR   pszProxyServer  = "proxy";
INT     iProxyPort      = 80;
*/

HCERTSTORE hMyCertStore = NULL;
HMODULE g_hSecurity     = NULL;

SCHANNEL_CRED SchannelCred;
PSecurityFunctionTable g_pSSPI = NULL;

struct tls_priv
{
    bool init_ok;

    WSADATA WsaData;
    SOCKET  Socket;

    CredHandle hClientCreds;
    CtxtHandle hContext;
    BOOL fCredsInitialized;
    BOOL fContextInitialized;

    SecBuffer  ExtraData;
    SECURITY_STATUS Status;

    PCCERT_CONTEXT pRemoteCertContext;
    SecPkgContext_StreamSizes Sizes;

    char *buffer;
    int cbIoBufferLength;

    tls_priv() {
        init_ok = false;
        Socket = INVALID_SOCKET;
        fCredsInitialized   = FALSE;
        fContextInitialized = FALSE;
        pRemoteCertContext = NULL;
        buffer = NULL;
        cbIoBufferLength = 0;
    }
    ~tls_priv() { delete buffer; }
};

BOOL LoadSecurityLibrary( void ) // load SSPI.DLL, set up a special table - PSecurityFunctionTable
{
    if ( g_pSSPI == NULL ) {

        INIT_SECURITY_INTERFACE pInitSecurityInterface;
    //  QUERY_CREDENTIALS_ATTRIBUTES_FN pQueryCredentialsAttributes;
        OSVERSIONINFO VerInfo;
        char lpszDLL[MAX_PATH];


        //  Find out which security DLL to use, depending on
        //  whether we are on Win2K, NT or Win9x
        VerInfo.dwOSVersionInfoSize = sizeof (OSVERSIONINFO);
        if ( !GetVersionEx (&VerInfo) ) return FALSE;

        if ( VerInfo.dwPlatformId == VER_PLATFORM_WIN32_NT  &&  VerInfo.dwMajorVersion == 4 )
        {
            strcpy (lpszDLL, NT4_DLL_NAME ); // NT4_DLL_NAME TEXT("Security.dll")
        }
        else if ( VerInfo.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS ||
                  VerInfo.dwPlatformId == VER_PLATFORM_WIN32_NT )
        {
            strcpy(lpszDLL, DLL_NAME); // DLL_NAME TEXT("Secur32.dll")
        }
        else
        {
            WLOG << "LoadSecurityLibrary()-> System not recognized";
            return FALSE;
        }


        //  Load Security DLL
        g_hSecurity = LoadLibrary(lpszDLL);
        if(g_hSecurity == NULL) {
            ELOG << "LoadSecurityLibrary()-> Error " << GetLastError() << " loading " << lpszDLL;
            return FALSE;
        }

        pInitSecurityInterface = (INIT_SECURITY_INTERFACE)GetProcAddress( g_hSecurity, "InitSecurityInterfaceA" );
        if(pInitSecurityInterface == NULL) {
            ELOG << "LoadSecurityLibrary()-> Error " << GetLastError() << " reading InitSecurityInterface entry point";
            return FALSE;
        }
        g_pSSPI = pInitSecurityInterface(); // call InitSecurityInterfaceA(void);
        if(g_pSSPI == NULL) {
            ELOG << "LoadSecurityLibrary()-> Error " << GetLastError() << " reading security interface";
            return FALSE;
        }
    }
    return TRUE; // and PSecurityFunctionTable
}

/*****************************************************************************/
static SECURITY_STATUS CreateCredentials( LPSTR pszUser, PCredHandle phCreds )
{ //                                                in                     out
    TimeStamp        tsExpiry;
    SECURITY_STATUS  Status;
    DWORD            cSupportedAlgs = 0;
    ALG_ID           rgbSupportedAlgs[16];
    PCCERT_CONTEXT   pCertContext = NULL;


    // Open the "MY" certificate store, where IE stores client certificates.
        // Windows maintains 4 stores -- MY, CA, ROOT, SPC.
    if(hMyCertStore == NULL)
    {
        hMyCertStore = CertOpenSystemStore(0, "MY");
        if(!hMyCertStore)
        {
            ELOG << "CreateCredentials()-> Error "<<GetLastError()<<" returned by CertOpenSystemStore";
            return SEC_E_NO_CREDENTIALS;
        }
    }


    // If a user name is specified, then attempt to find a client
    // certificate. Otherwise, just create a NULL credential.
    if(pszUser)
    {
        // Find client certificate. Note that this sample just searches for a
        // certificate that contains the user name somewhere in the subject name.
        // A real application should be a bit less casual.
        pCertContext = CertFindCertificateInStore( hMyCertStore,                     // hCertStore
                                                   X509_ASN_ENCODING,             // dwCertEncodingType
                                                   0,                                             // dwFindFlags
                                                   CERT_FIND_SUBJECT_STR_A,// dwFindType
                                                   pszUser,                         // *pvFindPara
                                                   NULL );                                 // pPrevCertContext


        if(pCertContext == NULL)
        {
            ELOG << "CreateCredentials()-> Error "<<GetLastError()<<" returned by CertFindCertificateInStore";
            if( GetLastError() == CRYPT_E_NOT_FOUND ) WLOG << "CRYPT_E_NOT_FOUND - property doesn't exist";
            return SEC_E_NO_CREDENTIALS;
        }
    }


    // Build Schannel credential structure. Currently, this sample only
    // specifies the protocol to be used (and optionally the certificate,
    // of course). Real applications may wish to specify other parameters as well.
    ZeroMemory( &SchannelCred, sizeof(SchannelCred) );

    SchannelCred.dwVersion  = SCHANNEL_CRED_VERSION;
    if(pCertContext)
    {
        SchannelCred.cCreds     = 1;
        SchannelCred.paCred     = &pCertContext;
    }

    SchannelCred.grbitEnabledProtocols = dwProtocol;

    if(aiKeyExch) rgbSupportedAlgs[cSupportedAlgs++] = aiKeyExch;

    if(cSupportedAlgs)
    {
        SchannelCred.cSupportedAlgs    = cSupportedAlgs;
        SchannelCred.palgSupportedAlgs = rgbSupportedAlgs;
    }

    SchannelCred.dwFlags |= SCH_CRED_NO_DEFAULT_CREDS;

    // The SCH_CRED_MANUAL_CRED_VALIDATION flag is specified because
    // this sample verifies the server certificate manually.
    // Applications that expect to run on WinNT, Win9x, or WinME
    // should specify this flag and also manually verify the server
    // certificate. Applications running on newer versions of Windows can
    // leave off this flag, in which case the InitializeSecurityContext
    // function will validate the server certificate automatically.
    SchannelCred.dwFlags |= SCH_CRED_MANUAL_CRED_VALIDATION;


    // Create an SSPI credential.
    Status = g_pSSPI->AcquireCredentialsHandleA( NULL,                 // Name of principal
                                                                                                 (char*)UNISP_NAME_A,         // Name of package
                                                                                                 SECPKG_CRED_OUTBOUND, // Flags indicating use
                                                                                                 NULL,                 // Pointer to logon ID
                                                                                                 &SchannelCred,        // Package specific data
                                                                                                 NULL,                 // Pointer to GetKey() func
                                                                                                 NULL,                 // Value to pass to GetKey()
                                                                                                 phCreds,              // (out) Cred Handle
                                                                                                 &tsExpiry );          // (out) Lifetime (optional)

    if(Status != SEC_E_OK)
        ELOG << "CreateCredentials()-> Error "<<Status<<" returned by AcquireCredentialsHandle";

    // cleanup: Free the certificate context. Schannel has already made its own copy.
    if(pCertContext) CertFreeCertificateContext(pCertContext);

    return Status;
}

/*****************************************************************************/
static INT ConnectToServer( const char * pszServerName, INT iPortNumber, SOCKET * pSocket )
{ //                                    in                in                 out
    SOCKET Socket;
    struct sockaddr_in sin;
    struct hostent *hp;


    Socket = socket(PF_INET, SOCK_STREAM, 0);
    if(Socket == INVALID_SOCKET)
    {
        ELOG << "ConnectToServer()-> Error "<<WSAGetLastError()<<" creating socket";
//        printf("**** Error %d creating socket\n", WSAGetLastError());
//              DisplayWinSockError( WSAGetLastError() );
        return WSAGetLastError();
    }

/*
    if(fUseProxy)
    {
        sin.sin_family = AF_INET;
        sin.sin_port = ntohs((u_short)iProxyPort);
        if((hp = gethostbyname(pszProxyServer)) == NULL)
        {
          printf("**** Error %d returned by gethostbyname using Proxy\n", WSAGetLastError());
                    DisplayWinSockError( WSAGetLastError() );
          return WSAGetLastError();
        }
        else
          memcpy(&sin.sin_addr, hp->h_addr, 4);
    }

    else // No proxy used
        */
    {
        sin.sin_family = AF_INET;
        sin.sin_port = htons((u_short)iPortNumber);
        if((hp = gethostbyname(pszServerName)) == NULL)
        {
            ELOG << "ConnectToServer()-> Error returned by gethostbyname";
//          printf("**** Error returned by gethostbyname\n");
//                    DisplayWinSockError( WSAGetLastError() );
          return WSAGetLastError();
        }
        else
          memcpy(&sin.sin_addr, hp->h_addr, 4);
    }


    if(connect(Socket, (struct sockaddr *)&sin, sizeof(sin)) == SOCKET_ERROR)
    {
        //printf( "**** Error %d connecting to \"%s\" (%s)\n",  WSAGetLastError(), pszServerName,  inet_ntoa(sin.sin_addr) );
        ELOG << "ConnectToServer()-> Error "<<WSAGetLastError()<<" connecting to \"" << pszServerName << "\" (" << inet_ntoa(sin.sin_addr) << ")";
        closesocket(Socket);
                //DisplayWinSockError( WSAGetLastError() );
        return WSAGetLastError();
    }

/*
    if(fUseProxy)
    {
        BYTE  pbMessage[200];
        DWORD cbMessage;

        // Build message for proxy server
        strcpy(pbMessage, "CONNECT ");
        strcat(pbMessage, pszServerName);
        strcat(pbMessage, ":");
         _itoa(iPortNumber, pbMessage + strlen(pbMessage), 10);
        strcat(pbMessage, " HTTP/1.0\r\nUser-Agent: webclient\r\n\r\n");
        cbMessage = (DWORD)strlen(pbMessage);

        // Send message to proxy server
        if(send(Socket, pbMessage, cbMessage, 0) == SOCKET_ERROR)
        {
          printf("**** Error %d sending message to proxy!\n", WSAGetLastError());
                    DisplayWinSockError( WSAGetLastError() );
                    return WSAGetLastError();
        }

        // Receive message from proxy server
        cbMessage = recv(Socket, pbMessage, 200, 0);
        if(cbMessage == SOCKET_ERROR)
        {
          printf("**** Error %d receiving message from proxy\n", WSAGetLastError());
                    DisplayWinSockError( WSAGetLastError() );
                    return WSAGetLastError();
        }
        // this sample is limited but in normal use it
        // should continue to receive until CR LF CR LF is received
    }*/
    *pSocket = Socket;

  return SEC_E_OK;
}


/*****************************************************************************/
static void GetNewClientCredentials( CredHandle *phCreds, CtxtHandle *phContext )
{

    CredHandle                                            hCreds;
    SecPkgContext_IssuerListInfoEx    IssuerListInfo;
    PCCERT_CHAIN_CONTEXT                        pChainContext;
    CERT_CHAIN_FIND_BY_ISSUER_PARA    FindByIssuerPara;
    PCCERT_CONTEXT                                    pCertContext;
    TimeStamp                                                tsExpiry;
    SECURITY_STATUS                                    Status;


    // Read list of trusted issuers from schannel.
    Status = g_pSSPI->QueryContextAttributes( phContext, SECPKG_ATTR_ISSUER_LIST_EX, (PVOID)&IssuerListInfo );
        if(Status != SEC_E_OK) { ELOG << "GetNewClientCredentials()-> Error "<<Status<<" querying issuer list info"; return; }

    // Enumerate the client certificates.
    ZeroMemory(&FindByIssuerPara, sizeof(FindByIssuerPara));

    FindByIssuerPara.cbSize = sizeof(FindByIssuerPara);
    FindByIssuerPara.pszUsageIdentifier = szOID_PKIX_KP_CLIENT_AUTH;
    FindByIssuerPara.dwKeySpec = 0;
    FindByIssuerPara.cIssuer   = IssuerListInfo.cIssuers;
    FindByIssuerPara.rgIssuer  = IssuerListInfo.aIssuers;

    pChainContext = NULL;

    while(TRUE)
    {   // Find a certificate chain.
        pChainContext = CertFindChainInStore( hMyCertStore,
                                              X509_ASN_ENCODING,
                                              0,
                                              CERT_CHAIN_FIND_BY_ISSUER,
                                              &FindByIssuerPara,
                                              pChainContext );
                if(pChainContext == NULL) {
                    ELOG << "GetNewClientCredentials()->Error "<<GetLastError()<<" finding cert chain"; break; }

                //printf("\ncertificate chain found\n");

        // Get pointer to leaf certificate context.
        pCertContext = pChainContext->rgpChain[0]->rgpElement[0]->pCertContext;

        // Create schannel credential.
        SchannelCred.dwVersion = SCHANNEL_CRED_VERSION;
        SchannelCred.cCreds = 1;
        SchannelCred.paCred = &pCertContext;

        Status = g_pSSPI->AcquireCredentialsHandleA(  NULL,                   // Name of principal
                                                                                                            (char*)UNISP_NAME_A,           // Name of package
                                                                                                            SECPKG_CRED_OUTBOUND,   // Flags indicating use
                                                                                                            NULL,                   // Pointer to logon ID
                                                                                                            &SchannelCred,          // Package specific data
                                                                                                            NULL,                   // Pointer to GetKey() func
                                                                                                            NULL,                   // Value to pass to GetKey()
                                                                                                            &hCreds,                // (out) Cred Handle
                                                                                                            &tsExpiry );            // (out) Lifetime (optional)

                if(Status != SEC_E_OK) {ELOG << "GetNewClientCredentials()-> Error "<<Status<<" returned by AcquireCredentialsHandle"; continue;}

                //printf("\nnew schannel credential created\n");

        g_pSSPI->FreeCredentialsHandle(phCreds); // Destroy the old credentials.

        *phCreds = hCreds;

    }
}


/*****************************************************************************/
static SECURITY_STATUS ClientHandshakeLoop( SOCKET          Socket,         // in
                                                                                        PCredHandle     phCreds,        // in
                                                                                        CtxtHandle *    phContext,      // in, out
                                                                                        BOOL            fDoInitialRead, // in
                                                                                        SecBuffer *     pExtraData )    // out

{

      SecBufferDesc   OutBuffer, InBuffer;
    SecBuffer       InBuffers[2], OutBuffers[1];
    DWORD           dwSSPIFlags, dwSSPIOutFlags, cbData, cbIoBuffer;
    TimeStamp       tsExpiry;
    SECURITY_STATUS scRet;
    PUCHAR          IoBuffer;
    BOOL            fDoRead;


    dwSSPIFlags = ISC_REQ_SEQUENCE_DETECT   | ISC_REQ_REPLAY_DETECT     | ISC_REQ_CONFIDENTIALITY   |
                  ISC_RET_EXTENDED_ERROR    | ISC_REQ_ALLOCATE_MEMORY   | ISC_REQ_STREAM;


    // Allocate data buffer.
    IoBuffer = (PUCHAR)LocalAlloc(LMEM_FIXED, IO_BUFFER_SIZE);
    if(IoBuffer == NULL) { ELOG << "ClientHandshakeLoop()-> **** Out of memory (1)"; return SEC_E_INTERNAL_ERROR; }
    cbIoBuffer = 0;
    fDoRead = fDoInitialRead;



    // Loop until the handshake is finished or an error occurs.
    scRet = SEC_I_CONTINUE_NEEDED;

    while( scRet == SEC_I_CONTINUE_NEEDED        ||
           scRet == SEC_E_INCOMPLETE_MESSAGE     ||
           scRet == SEC_I_INCOMPLETE_CREDENTIALS )
   {
        if(0 == cbIoBuffer || scRet == SEC_E_INCOMPLETE_MESSAGE) // Read data from server.
        {
            if(fDoRead)
            {
                cbData = recv(Socket, (char*)(IoBuffer + cbIoBuffer), IO_BUFFER_SIZE - cbIoBuffer, 0 );
                if(cbData == SOCKET_ERROR)
                {
                    ELOG << "ClientHandshakeLoop()->Error "<<WSAGetLastError()<<" reading data from server";
                    scRet = SEC_E_INTERNAL_ERROR;
                    break;
                }
                else if(cbData == 0)
                {
                    ELOG << "ClientHandshakeLoop()->Server unexpectedly disconnected";
                    scRet = SEC_E_INTERNAL_ERROR;
                    break;
                }
                DLOG << "ClientHandshakeLoop()->"<<cbData<<" bytes of handshake data received";
                //if(fVerbose) { PrintHexDump(cbData, IoBuffer + cbIoBuffer); printf("\n"); }
                cbIoBuffer += cbData;
            }
            else
              fDoRead = TRUE;
        }



        // Set up the input buffers. Buffer 0 is used to pass in data
        // received from the server. Schannel will consume some or all
        // of this. Leftover data (if any) will be placed in buffer 1 and
        // given a buffer type of SECBUFFER_EXTRA.
        InBuffers[0].pvBuffer   = IoBuffer;
        InBuffers[0].cbBuffer   = cbIoBuffer;
        InBuffers[0].BufferType = SECBUFFER_TOKEN;

        InBuffers[1].pvBuffer   = NULL;
        InBuffers[1].cbBuffer   = 0;
        InBuffers[1].BufferType = SECBUFFER_EMPTY;

        InBuffer.cBuffers       = 2;
        InBuffer.pBuffers       = InBuffers;
        InBuffer.ulVersion      = SECBUFFER_VERSION;


        // Set up the output buffers. These are initialized to NULL
        // so as to make it less likely we'll attempt to free random
        // garbage later.
        OutBuffers[0].pvBuffer  = NULL;
        OutBuffers[0].BufferType= SECBUFFER_TOKEN;
        OutBuffers[0].cbBuffer  = 0;

        OutBuffer.cBuffers      = 1;
        OutBuffer.pBuffers      = OutBuffers;
        OutBuffer.ulVersion     = SECBUFFER_VERSION;


        // Call InitializeSecurityContext.
        scRet = g_pSSPI->InitializeSecurityContextA(  phCreds,
                                                                                                            phContext,
                                                                                                            NULL,
                                                                                                            dwSSPIFlags,
                                                                                                            0,
                                                                                                            SECURITY_NATIVE_DREP,
                                                                                                            &InBuffer,
                                                                                                            0,
                                                                                                            NULL,
                                                                                                            &OutBuffer,
                                                                                                            &dwSSPIOutFlags,
                                                                                                            &tsExpiry );


        // If InitializeSecurityContext was successful (or if the error was
        // one of the special extended ones), send the contends of the output
        // buffer to the server.
        if(scRet == SEC_E_OK                ||
           scRet == SEC_I_CONTINUE_NEEDED   ||
           FAILED(scRet) && (dwSSPIOutFlags & ISC_RET_EXTENDED_ERROR))
        {
            if(OutBuffers[0].cbBuffer != 0 && OutBuffers[0].pvBuffer != NULL)
            {
                cbData = send(Socket, (const char *)OutBuffers[0].pvBuffer, OutBuffers[0].cbBuffer, 0 );
                if(cbData == SOCKET_ERROR || cbData == 0)
                {
                    ELOG << "ClientHandshakeLoop()->Error "<<WSAGetLastError()<<" sending data to server (2)";
//                    printf( "**** Error %d sending data to server (2)\n",  WSAGetLastError() );
//                                        DisplayWinSockError( WSAGetLastError() );
                    g_pSSPI->FreeContextBuffer(OutBuffers[0].pvBuffer);
                    g_pSSPI->DeleteSecurityContext(phContext);
                    return SEC_E_INTERNAL_ERROR;
                }
                DLOG << "ClientHandshakeLoop()-> "<<cbData<<" bytes of handshake data sent";
                //if(fVerbose) { PrintHexDump(cbData, OutBuffers[0].pvBuffer); printf("\n"); }

                // Free output buffer.
                g_pSSPI->FreeContextBuffer(OutBuffers[0].pvBuffer);
                OutBuffers[0].pvBuffer = NULL;
            }
        }



        // If InitializeSecurityContext returned SEC_E_INCOMPLETE_MESSAGE,
        // then we need to read more data from the server and try again.
        if(scRet == SEC_E_INCOMPLETE_MESSAGE) continue;


        // If InitializeSecurityContext returned SEC_E_OK, then the
        // handshake completed successfully.
        if(scRet == SEC_E_OK)
        {
            // If the "extra" buffer contains data, this is encrypted application
            // protocol layer stuff. It needs to be saved. The application layer
            // will later decrypt it with DecryptMessage.
            DLOG << "ClientHandshakeLoop()->Handshake was successful";

            if(InBuffers[1].BufferType == SECBUFFER_EXTRA)
            {
                pExtraData->pvBuffer = LocalAlloc( LMEM_FIXED, InBuffers[1].cbBuffer );
                if(pExtraData->pvBuffer == NULL) { ELOG << "ClientHandshakeLoop()-> **** Out of memory (2)"; return SEC_E_INTERNAL_ERROR; }

                MoveMemory( pExtraData->pvBuffer,
                            IoBuffer + (cbIoBuffer - InBuffers[1].cbBuffer),
                            InBuffers[1].cbBuffer );

                pExtraData->cbBuffer   = InBuffers[1].cbBuffer;
                pExtraData->BufferType = SECBUFFER_TOKEN;

                DLOG << "ClientHandshakeLoop()-> "<<pExtraData->cbBuffer<<" bytes of app data was bundled with handshake data";
            }
            else
            {
                pExtraData->pvBuffer   = NULL;
                pExtraData->cbBuffer   = 0;
                pExtraData->BufferType = SECBUFFER_EMPTY;
            }
            break; // Bail out to quit
        }



        // Check for fatal error.
        if(FAILED(scRet)) { ELOG << "ClientHandshakeLoop()->Error "<<scRet<<" returned by InitializeSecurityContext (2)"; break; }

        // If InitializeSecurityContext returned SEC_I_INCOMPLETE_CREDENTIALS,
        // then the server just requested client authentication.
        if(scRet == SEC_I_INCOMPLETE_CREDENTIALS)
        {
            // Busted. The server has requested client authentication and
            // the credential we supplied didn't contain a client certificate.
            // This function will read the list of trusted certificate
            // authorities ("issuers") that was received from the server
            // and attempt to find a suitable client certificate that
            // was issued by one of these. If this function is successful,
            // then we will connect using the new certificate. Otherwise,
            // we will attempt to connect anonymously (using our current credentials).
            GetNewClientCredentials(phCreds, phContext);

            // Go around again.
            fDoRead = FALSE;
            scRet = SEC_I_CONTINUE_NEEDED;
            continue;
        }

        // Copy any leftover data from the "extra" buffer, and go around again.
        if ( InBuffers[1].BufferType == SECBUFFER_EXTRA )
        {
            MoveMemory( IoBuffer, IoBuffer + (cbIoBuffer - InBuffers[1].cbBuffer), InBuffers[1].cbBuffer );
            cbIoBuffer = InBuffers[1].cbBuffer;
        }
        else
          cbIoBuffer = 0;
    }

    // Delete the security context in the case of a fatal error.
    if(FAILED(scRet)) g_pSSPI->DeleteSecurityContext(phContext);
    LocalFree(IoBuffer);

    return scRet;
}


/*****************************************************************************/
static SECURITY_STATUS PerformClientHandshake( SOCKET          Socket,        // in
                                                                                             PCredHandle     phCreds,       // in
                                                                                             const char *    pszServerName, // in
                                                                                             CtxtHandle *    phContext,     // out
                                                                                             SecBuffer *     pExtraData )   // out
{

    SecBufferDesc   OutBuffer;
    SecBuffer       OutBuffers[1];
    DWORD           dwSSPIFlags, dwSSPIOutFlags, cbData;
    TimeStamp       tsExpiry;
    SECURITY_STATUS scRet;


    dwSSPIFlags = ISC_REQ_SEQUENCE_DETECT   | ISC_REQ_REPLAY_DETECT     | ISC_REQ_CONFIDENTIALITY   |
                  ISC_RET_EXTENDED_ERROR    | ISC_REQ_ALLOCATE_MEMORY   | ISC_REQ_STREAM;


    //  Initiate a ClientHello message and generate a token.
    OutBuffers[0].pvBuffer   = NULL;
    OutBuffers[0].BufferType = SECBUFFER_TOKEN;
    OutBuffers[0].cbBuffer   = 0;

    OutBuffer.cBuffers  = 1;
    OutBuffer.pBuffers  = OutBuffers;
    OutBuffer.ulVersion = SECBUFFER_VERSION;

    scRet = g_pSSPI->InitializeSecurityContextA(  phCreds,
                                                                                                    NULL,
                                                                                                    const_cast<char*>(pszServerName),
                                                                                                    dwSSPIFlags,
                                                                                                    0,
                                                                                                    SECURITY_NATIVE_DREP,
                                                                                                    NULL,
                                                                                                    0,
                                                                                                    phContext,
                                                                                                    &OutBuffer,
                                                                                                    &dwSSPIOutFlags,
                                                                                                    &tsExpiry );

    if(scRet != SEC_I_CONTINUE_NEEDED) { ELOG << "PerformClientHandshake()->Error "<<scRet<<" returned by InitializeSecurityContext (1)"; return scRet; }

    // Send response to server if there is one.
    if(OutBuffers[0].cbBuffer != 0 && OutBuffers[0].pvBuffer != NULL)
    {
        cbData = send( Socket, (const char *)OutBuffers[0].pvBuffer, OutBuffers[0].cbBuffer, 0 );
        if( cbData == SOCKET_ERROR || cbData == 0 )
        {
            ELOG << "PerformClientHandshake()->Error "<<WSAGetLastError()<<" sending data to server (1)";
            g_pSSPI->FreeContextBuffer(OutBuffers[0].pvBuffer);
            g_pSSPI->DeleteSecurityContext(phContext);
            return SEC_E_INTERNAL_ERROR;
        }
        DLOG << "PerformClientHandshake()-> "<<cbData<<" bytes of handshake data sent";
        //        if(fVerbose) { PrintHexDump(cbData, OutBuffers[0].pvBuffer); printf("\n"); }
        g_pSSPI->FreeContextBuffer(OutBuffers[0].pvBuffer); // Free output buffer.
        OutBuffers[0].pvBuffer = NULL;
    }

    return ClientHandshakeLoop(Socket, phCreds, phContext, TRUE, pExtraData);
}

/*****************************************************************************/
static DWORD VerifyServerCertificate( PCCERT_CONTEXT pServerCert, PSTR pszServerName, DWORD dwCertFlags )
{
    HTTPSPolicyCallbackData  polHttps;
    CERT_CHAIN_POLICY_PARA   PolicyPara;
    CERT_CHAIN_POLICY_STATUS PolicyStatus;
    CERT_CHAIN_PARA          ChainPara;
    PCCERT_CHAIN_CONTEXT     pChainContext = NULL;
    DWORD                                         cchServerName, Status;
    LPSTR rgszUsages[]     = { (char*)szOID_PKIX_KP_SERVER_AUTH,
                               (char*)szOID_SERVER_GATED_CRYPTO,
                               (char*)szOID_SGC_NETSCAPE };

    DWORD cUsages          = sizeof(rgszUsages) / sizeof(LPSTR);

    PWSTR   pwszServerName = NULL;


    if(pServerCert == NULL)
    { Status = SEC_E_WRONG_PRINCIPAL; goto cleanup; }

    // Convert server name to unicode.
    if(pszServerName == NULL || strlen(pszServerName) == 0)
    { Status = SEC_E_WRONG_PRINCIPAL; goto cleanup; }

    cchServerName = MultiByteToWideChar(CP_ACP, 0, pszServerName, -1, NULL, 0);
    pwszServerName = (PWSTR)LocalAlloc(LMEM_FIXED, cchServerName * sizeof(WCHAR));
    if(pwszServerName == NULL)
    { Status = SEC_E_INSUFFICIENT_MEMORY; goto cleanup; }

    cchServerName = MultiByteToWideChar(CP_ACP, 0, pszServerName, -1, pwszServerName, cchServerName);
    if(cchServerName == 0)
    { Status = SEC_E_WRONG_PRINCIPAL; goto cleanup; }


    // Build certificate chain.
    ZeroMemory(&ChainPara, sizeof(ChainPara));
    ChainPara.cbSize = sizeof(ChainPara);
    ChainPara.RequestedUsage.dwType = USAGE_MATCH_TYPE_OR;
    ChainPara.RequestedUsage.Usage.cUsageIdentifier     = cUsages;
    ChainPara.RequestedUsage.Usage.rgpszUsageIdentifier = rgszUsages;

    if( !CertGetCertificateChain( NULL,
                                                                    pServerCert,
                                                                    NULL,
                                                                    pServerCert->hCertStore,
                                                                    &ChainPara,
                                                                    0,
                                                                    NULL,
                                                                    &pChainContext ) )
    {
        Status = GetLastError();
        ELOG << "VerifyServerCertificate()-> Error "<<Status<<" returned by CertGetCertificateChain!";
        goto cleanup;
    }


    // Validate certificate chain.
    ZeroMemory(&polHttps, sizeof(HTTPSPolicyCallbackData));
    polHttps.cbStruct           = sizeof(HTTPSPolicyCallbackData);
    polHttps.dwAuthType         = AUTHTYPE_SERVER;
    polHttps.fdwChecks          = dwCertFlags;
    polHttps.pwszServerName     = pwszServerName;

    memset(&PolicyPara, 0, sizeof(PolicyPara));
    PolicyPara.cbSize            = sizeof(PolicyPara);
    PolicyPara.pvExtraPolicyPara = &polHttps;

    memset(&PolicyStatus, 0, sizeof(PolicyStatus));
    PolicyStatus.cbSize = sizeof(PolicyStatus);

    if( !CertVerifyCertificateChainPolicy( CERT_CHAIN_POLICY_SSL,
                                                                                        pChainContext,
                                                                                        &PolicyPara,
                                                                                        &PolicyStatus ) )
    {
        Status = GetLastError();
        ELOG << "VerifyServerCertificate()-> Error "<<Status<<" returned by CertVerifyCertificateChainPolicy!";
        goto cleanup;
    }

    if(PolicyStatus.dwError)
    {
        Status = PolicyStatus.dwError;
        WLOG << "VerifyServerCertificate()-> PolicyStatus.dwError: "<<Status;
        //DisplayWinVerifyTrustError(Status);
        goto cleanup;
    }

    Status = SEC_E_OK;


cleanup:
    if(pChainContext)  CertFreeCertificateChain(pChainContext);
    if(pwszServerName) LocalFree(pwszServerName);

    return Status;
}

/*****************************************************************************/
static SECURITY_STATUS ReadDecrypt( SOCKET Socket, PCredHandle phCreds, CtxtHandle * phContext, PBYTE pbIoBuffer, DWORD    cbIoBufferLength, std::string *resp )

// calls recv() - blocking socket read
// http://msdn.microsoft.com/en-us/library/ms740121(VS.85).aspx

// The encrypted message is decrypted in place, overwriting the original contents of its buffer.
// http://msdn.microsoft.com/en-us/library/aa375211(VS.85).aspx

{
  SecBuffer                ExtraBuffer;
  SecBuffer                *pDataBuffer, *pExtraBuffer;

  SECURITY_STATUS    scRet;            // unsigned long cbBuffer;    // Size of the buffer, in bytes
  SecBufferDesc        Message;        // unsigned long BufferType;  // Type of the buffer (below)
  SecBuffer                Buffers[4];    // void    SEC_FAR * pvBuffer;   // Pointer to the buffer

  DWORD                        cbIoBuffer, cbData, length;
    PBYTE                        buff;
  int i;



    // Read data from server until done.
    cbIoBuffer = 0;
        scRet = 0;
    while(TRUE) // Read some data.
    {
                if( cbIoBuffer == 0 || scRet == SEC_E_INCOMPLETE_MESSAGE ) // get the data
        {
            cbData = recv(Socket, (char*)(pbIoBuffer + cbIoBuffer), cbIoBufferLength - cbIoBuffer, 0);
            if(cbData == SOCKET_ERROR)
            {
                ELOG << "ReadDecrypt()->Error "<<WSAGetLastError()<<" reading data from server";
                scRet = SEC_E_INTERNAL_ERROR;
                break;
            }
            else if(cbData == 0) // Server disconnected.
            {
                if(cbIoBuffer)
                {
                    ELOG << "ReadDecrypt()->Server unexpectedly disconnected";
                    scRet = SEC_E_INTERNAL_ERROR;
                    return scRet;
                }
                else
                  break; // All Done
            }
            else // success
            {
                DLOG << "ReadDecrypt()-> "<<cbData<<" bytes of (encrypted) application data received";
                //if(fVerbose) { PrintHexDump(cbData, pbIoBuffer + cbIoBuffer); printf("\n"); }
                cbIoBuffer += cbData;
            }
        }


        // Decrypt the received data.
        Buffers[0].pvBuffer     = pbIoBuffer;
        Buffers[0].cbBuffer     = cbIoBuffer;
        Buffers[0].BufferType   = SECBUFFER_DATA;  // Initial Type of the buffer 1
                Buffers[1].BufferType   = SECBUFFER_EMPTY; // Initial Type of the buffer 2
                Buffers[2].BufferType   = SECBUFFER_EMPTY; // Initial Type of the buffer 3
                Buffers[3].BufferType   = SECBUFFER_EMPTY; // Initial Type of the buffer 4

                Message.ulVersion       = SECBUFFER_VERSION;    // Version number
                Message.cBuffers        = 4;                                    // Number of buffers - must contain four SecBuffer structures.
                Message.pBuffers        = Buffers;                        // Pointer to array of buffers
        scRet = g_pSSPI->DecryptMessage(phContext, &Message, 0, NULL);
        if( scRet == SEC_I_CONTEXT_EXPIRED ) break; // Server signalled end of session
//      if( scRet == SEC_E_INCOMPLETE_MESSAGE - Input buffer has partial encrypted record, read more
        if( scRet != SEC_E_OK &&
            scRet != SEC_I_RENEGOTIATE &&
            scRet != SEC_I_CONTEXT_EXPIRED )
                        { WLOG << "ReadDecrypt()->Error "<<scRet<<" in DecryptMessage";
                            //DisplaySECError((DWORD)scRet);
                            return scRet; }



        // Locate data and (optional) extra buffers.
        pDataBuffer  = NULL;
        pExtraBuffer = NULL;
        for(i = 1; i < 4; i++)
        {
            if( pDataBuffer  == NULL && Buffers[i].BufferType == SECBUFFER_DATA  ) pDataBuffer  = &Buffers[i];
            if( pExtraBuffer == NULL && Buffers[i].BufferType == SECBUFFER_EXTRA ) pExtraBuffer = &Buffers[i];
        }


        // Display the decrypted data.
        if(pDataBuffer)
        {
            if (resp) resp->append((const char *)pDataBuffer->pvBuffer, pDataBuffer->cbBuffer);

                    length = pDataBuffer->cbBuffer;
                    if( length ) // check if last two chars are CR LF
                    {
                        buff = (PBYTE)pDataBuffer->pvBuffer; // printf( "n-2= %d, n-1= %d \n", buff[length-2], buff[length-1] );
                        DLOG << "ReadDecrypt()->Decrypted data: "<<length<<" bytes"; //PrintText( length, buff );
                        //if(fVerbose) { PrintHexDump(length, buff); printf("\n"); }
                        if( buff[length-2] == 13 && buff[length-1] == 10 ) break; // printf("Found CRLF\n");
                    }
        }



        // Move any "extra" data to the input buffer.
        if(pExtraBuffer)
        {
            MoveMemory(pbIoBuffer, pExtraBuffer->pvBuffer, pExtraBuffer->cbBuffer);
            cbIoBuffer = pExtraBuffer->cbBuffer; // printf("cbIoBuffer= %d  \n", cbIoBuffer);
        }
        else
          cbIoBuffer = 0;


                // The server wants to perform another handshake sequence.
        if(scRet == SEC_I_RENEGOTIATE)
        {
            ILOG << "ReadDecrypt()->Server requested renegotiate!";
            scRet = ClientHandshakeLoop( Socket, phCreds, phContext, FALSE, &ExtraBuffer);
            if(scRet != SEC_E_OK) return scRet;

            if(ExtraBuffer.pvBuffer) // Move any "extra" data to the input buffer.
            {
                MoveMemory(pbIoBuffer, ExtraBuffer.pvBuffer, ExtraBuffer.cbBuffer);
                cbIoBuffer = ExtraBuffer.cbBuffer;
            }
        }
    } // Loop till CRLF is found at the end of the data

    return SEC_E_OK;
}

/*****************************************************************************/
static DWORD EncryptSend( SOCKET Socket, CtxtHandle * phContext, PBYTE pbIoBuffer, SecPkgContext_StreamSizes Sizes, int len )
// http://msdn.microsoft.com/en-us/library/aa375378(VS.85).aspx
// The encrypted message is encrypted in place, overwriting the original contents of its buffer.
{
    SECURITY_STATUS    scRet;            // unsigned long cbBuffer;    // Size of the buffer, in bytes
    SecBufferDesc        Message;        // unsigned long BufferType;  // Type of the buffer (below)
    SecBuffer                Buffers[4];    // void    SEC_FAR * pvBuffer;   // Pointer to the buffer
    DWORD                        cbMessage, cbData;
    PBYTE                        pbMessage;


    pbMessage = pbIoBuffer + Sizes.cbHeader; // Offset by "header size"
    cbMessage = len > 0 ? len : (DWORD)strlen((const char *)pbMessage);
    DLOG << "EncryptSend()->Sending "<<cbMessage<<" bytes of of plaintext";
    //    printf("Sending %d bytes of plaintext:", cbMessage); PrintText(cbMessage, pbMessage);
    //if(fVerbose) { PrintHexDump(cbMessage, pbMessage); printf("\n"); }


        // Encrypt the HTTP request.
    Buffers[0].pvBuffer     = pbIoBuffer;                                // Pointer to buffer 1
    Buffers[0].cbBuffer     = Sizes.cbHeader;                        // length of header
    Buffers[0].BufferType   = SECBUFFER_STREAM_HEADER;    // Type of the buffer

    Buffers[1].pvBuffer     = pbMessage;                                // Pointer to buffer 2
    Buffers[1].cbBuffer     = cbMessage;                                // length of the message
    Buffers[1].BufferType   = SECBUFFER_DATA;                        // Type of the buffer

    Buffers[2].pvBuffer     = pbMessage + cbMessage;        // Pointer to buffer 3
    Buffers[2].cbBuffer     = Sizes.cbTrailer;                    // length of the trailor
    Buffers[2].BufferType   = SECBUFFER_STREAM_TRAILER;    // Type of the buffer

        Buffers[3].pvBuffer     = SECBUFFER_EMPTY;                    // Pointer to buffer 4
    Buffers[3].cbBuffer     = SECBUFFER_EMPTY;                    // length of buffer 4
    Buffers[3].BufferType   = SECBUFFER_EMPTY;                    // Type of the buffer 4


    Message.ulVersion       = SECBUFFER_VERSION;    // Version number
    Message.cBuffers        = 4;                                    // Number of buffers - must contain four SecBuffer structures.
    Message.pBuffers        = Buffers;                        // Pointer to array of buffers
    scRet = g_pSSPI->EncryptMessage(phContext, 0, &Message, 0); // must contain four SecBuffer structures.
    if(FAILED(scRet)) {
        WLOG << "EncryptSend()->Error "<<scRet<<" returned by EncryptMessage";
        return scRet; }


    // Send the encrypted data to the server.                                            len                                                                         flags
    cbData = send( Socket, (const char *)pbIoBuffer,    Buffers[0].cbBuffer + Buffers[1].cbBuffer + Buffers[2].cbBuffer,    0 );

    DLOG << "EncryptSend()-> "<<cbData<<" bytes of encrypted data sent";
    //if(fVerbose) { PrintHexDump(cbData, pbIoBuffer); printf("\n"); }

    return cbData; // send( Socket, pbIoBuffer,    Sizes.cbHeader + strlen(pbMessage) + Sizes.cbTrailer,  0 );

}




tls_smtp_comm::tls_smtp_comm(int p, const std::string &s)
{
    port = p;
    server = s;
    tls_con = new tls_priv;

    try {
    if( !LoadSecurityLibrary() )
        throw std::runtime_error("initializing the security library");

    // Create credentials.
    if(CreateCredentials(NULL, &tls_con->hClientCreds))
        throw std::runtime_error("creating credentials");
    tls_con->fCredsInitialized = TRUE; //

    // Connect to server.
    if(ConnectToServer(server.c_str(), port, &tls_con->Socket))
        throw std::runtime_error("connecting to server");

    // Perform handshake
    if( PerformClientHandshake( tls_con->Socket, &tls_con->hClientCreds, server.c_str(), &tls_con->hContext, &tls_con->ExtraData ) )
        throw std::runtime_error("performing handshake");
    tls_con->fContextInitialized = TRUE; //

    // Authenticate server's credentials. Get server's certificate.
    tls_con->Status = g_pSSPI->QueryContextAttributes( &tls_con->hContext, SECPKG_ATTR_REMOTE_CERT_CONTEXT, (PVOID)&tls_con->pRemoteCertContext );
    if(tls_con->Status != SEC_E_OK)
        throw std::runtime_error("querying remote certificate");

    //DisplayCertChain( pRemoteCertContext, FALSE ); //

    // Attempt to validate server certificate.
    tls_con->Status = VerifyServerCertificate( tls_con->pRemoteCertContext, const_cast<char*>(server.c_str()), 0 );
    if(tls_con->Status) throw std::runtime_error("authenticating server credentials!");
        // The server certificate did not validate correctly. At this point, we cannot tell
        // if we are connecting to the correct server, or if we are connecting to a
        // "man in the middle" attack server - Best to just abort the connection.
//printf("----- Server Certificate Verified\n");



    // Free the server certificate context.
    CertFreeCertificateContext(tls_con->pRemoteCertContext);
    tls_con->pRemoteCertContext = NULL; //
//printf("----- Server certificate context released \n");


    // Display connection info.
    // DisplayConnectionInfo(&tls_con->hContext); //
//printf("----- Secure Connection Info\n");
                    // unsigned long cbBuffer;    // Size of the buffer, in bytes
        SECURITY_STATUS                        scRet;            // unsigned long BufferType;  // Type of the buffer (below)


        // Read stream encryption properties.
        scRet = g_pSSPI->QueryContextAttributes( &tls_con->hContext, SECPKG_ATTR_STREAM_SIZES, &tls_con->Sizes );
        if(scRet != SEC_E_OK) throw std::runtime_error("reading SECPKG_ATTR_STREAM_SIZES!");
        //{ printf("**** Error 0x%x reading SECPKG_ATTR_STREAM_SIZES\n", scRet); return scRet; }


        // Create a buffer.
        tls_con->cbIoBufferLength = tls_con->Sizes.cbHeader  +  tls_con->Sizes.cbMaximumMessage  +  tls_con->Sizes.cbTrailer;
        //pbIoBuffer       = LocalAlloc(LMEM_FIXED, cbIoBufferLength);
        tls_con->buffer = new char[tls_con->cbIoBufferLength];
        //if(pbIoBuffer == NULL) { printf("**** Out of memory (2)\n"); return SEC_E_INTERNAL_ERROR; }

        scRet = ReadDecrypt( tls_con->Socket, &tls_con->hClientCreds, &tls_con->hContext, (PBYTE)tls_con->buffer, tls_con->cbIoBufferLength, NULL );
        if( scRet != SEC_E_OK ) throw std::runtime_error("first ReadDecrypt");

        if (tls_con->buffer) tls_con->init_ok = true;

    } catch (std::exception &e) {
        ELOG << "tls_smtp_comm::tls_smtp_comm("<<p<<", "<<s<<")->Error " << e.what();
    }
}

tls_smtp_comm::~tls_smtp_comm()
{
    if (tls_con) {
        if(tls_con->pRemoteCertContext) {
            CertFreeCertificateContext(tls_con->pRemoteCertContext);
            tls_con->pRemoteCertContext = NULL;
        }
        // Free SSPI context handle.
        if(tls_con->fContextInitialized) {
            g_pSSPI->DeleteSecurityContext(&tls_con->hContext);
            tls_con->fContextInitialized = FALSE;
        }
        // Free SSPI credentials handle.
        if(tls_con->fCredsInitialized) {
            g_pSSPI->FreeCredentialsHandle(&tls_con->hClientCreds);
            tls_con->fCredsInitialized = FALSE;
        }

        // Close socket.
        if(tls_con->Socket != INVALID_SOCKET) closesocket(tls_con->Socket);

    // Shutdown WinSock subsystem.
    //WSACleanup();

    // Close "MY" certificate store.
    if (hMyCertStore) CertCloseStore(hMyCertStore, 0); //FIXME: use this with local variable

    //UnloadSecurityLibrary();

        delete tls_con;
    }
}
//#include <iostream>
int tls_smtp_comm::command(const std::string &cmd)
{
    int ret = 0;
    if (!tls_con || !tls_con->init_ok)
        return 999;

    int len = sprintf( tls_con->buffer + tls_con->Sizes.cbHeader, "%s\r\n",  cmd.c_str() ); // message begins after the header

    // Send a request.
    int cbData = EncryptSend( tls_con->Socket, &tls_con->hContext, (PBYTE)tls_con->buffer, tls_con->Sizes, len );
    if(cbData == SOCKET_ERROR || cbData == 0) {
        WLOG << "tls_smtp_comm::command("<<cmd<<")->Error " << WSAGetLastError() << " sending data to server";
        return 999;
    }
    // Receive a Response
    std::string resp;
    SECURITY_STATUS scRet = ReadDecrypt( tls_con->Socket, &tls_con->hClientCreds, &tls_con->hContext, (PBYTE)tls_con->buffer, tls_con->cbIoBufferLength, &resp );
    if( scRet != SEC_E_OK ) {
        WLOG << "tls_smtp_comm::command("<<cmd<<")->Error " << WSAGetLastError() << " receiving/decrypting";
        return 999;
    }
//std::cout << resp;

    ret = atoi(resp.c_str());
    if (ret <= 0) {
        WLOG << "tls_smtp_comm::command("<<cmd<<")->Response not understood";
        return 999;
    }

    return ret;
}
int tls_smtp_comm::send(const std::string &data)
{
    if (!tls_con || !tls_con->init_ok)
        return -1;

    int n = 0;
    while (n < data.size()) {
        int size1 = data.size() - n;
        if (size1 > tls_con->Sizes.cbMaximumMessage) size1 = tls_con->Sizes.cbMaximumMessage;
        memcpy(tls_con->buffer + tls_con->Sizes.cbHeader, data.c_str() + n, size1);
        int r = EncryptSend( tls_con->Socket, &tls_con->hContext, (PBYTE)tls_con->buffer, tls_con->Sizes, size1 );
        if (r <= 0) return r;
        n += size1;
    }

    return n;
}


#ifndef NDEBUG
#include <boost/test/unit_test.hpp>


BOOST_AUTO_TEST_SUITE (main_test_suite_tlsclient)

BOOST_AUTO_TEST_CASE (tlsclient_1)
{
    WSADATA wsadata;
    BOOST_CHECK( WSAStartup(MAKEWORD(2,0), &wsadata) == 0);

    tls_smtp_comm tsc(465, "correo.sarenet.es");

    BOOST_CHECK( tsc.command("HELO localhost") < 400 );

    std::string data = "----- body of the email -----";
    BOOST_CHECK( tsc.send(data) == data.size() );

}

BOOST_AUTO_TEST_SUITE_END( )

#endif

#endif
