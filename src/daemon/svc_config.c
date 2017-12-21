#include <stdio.h>
#include <strsafe.h>
#include <tchar.h>
#include <windows.h>

#pragma comment(lib, "advapi32.lib")

TCHAR szCommand[10];
TCHAR szSvcName[80];

VOID __stdcall DisplayUsage(void);

VOID __stdcall DoQuerySvc(void);
VOID __stdcall DoUpdateSvcDesc(void);
VOID __stdcall DoDisableSvc(void);
VOID __stdcall DoEnableSvc(void);
VOID __stdcall DoDeleteSvc(void);

//
// Purpose:
//   Entry point function. Executes specified command from user.
//
// Parameters:
//   Command-line syntax is: svcconfig [command] [service_path]
//
// Return value:
//   None
//
void __cdecl _tmain(int argc, TCHAR *argv[]) {
  printf("\n");
  if (argc != 3) {
    printf("ERROR:\tIncorrect number of arguments\n\n");
    DisplayUsage();
    return;
  }

  StringCchCopy(szCommand, 10, argv[1]);
  StringCchCopy(szSvcName, 80, argv[2]);

  if (lstrcmpi(szCommand, TEXT("query")) == 0)
    DoQuerySvc();
  else if (lstrcmpi(szCommand, TEXT("describe")) == 0)
    DoUpdateSvcDesc();
  else if (lstrcmpi(szCommand, TEXT("disable")) == 0)
    DoDisableSvc();
  else if (lstrcmpi(szCommand, TEXT("enable")) == 0)
    DoEnableSvc();
  else if (lstrcmpi(szCommand, TEXT("delete")) == 0)
    DoDeleteSvc();
  else {
    _tprintf(TEXT("Unknown command (%s)\n\n"), szCommand);
    DisplayUsage();
  }
}

VOID __stdcall DisplayUsage() {
  printf("Description:\n");
  printf("\tCommand-line tool that configures a service.\n\n");
  printf("Usage:\n");
  printf("\tsvcconfig [command] [service_name]\n\n");
  printf("\t[command]\n");
  printf("\t  query\n");
  printf("\t  describe\n");
  printf("\t  disable\n");
  printf("\t  enable\n");
  printf("\t  delete\n");
}

//
// Purpose:
//   Retrieves and displays the current service configuration.
//
// Parameters:
//   None
//
// Return value:
//   None
//
VOID __stdcall DoQuerySvc() {
  SC_HANDLE schSCManager;
  SC_HANDLE schService;
  LPQUERY_SERVICE_CONFIG lpsc;
  LPSERVICE_DESCRIPTION lpsd;
  DWORD dwBytesNeeded, cbBufSize, dwError;

  // Get a handle to the SCM database.

  schSCManager = OpenSCManager(NULL, // local computer
                               NULL, // ServicesActive database
                               SC_MANAGER_ALL_ACCESS); // full access rights

  if (NULL == schSCManager) {
    printf("OpenSCManager failed (%d)\n", GetLastError());
    return;
  }

  // Get a handle to the service.

  schService = OpenService(schSCManager,          // SCM database
                           szSvcName,             // name of service
                           SERVICE_QUERY_CONFIG); // need query config access

  if (schService == NULL) {
    printf("OpenService failed (%d)\n", GetLastError());
    CloseServiceHandle(schSCManager);
    return;
  }

  // Get the configuration information.

  if (!QueryServiceConfig(schService, NULL, 0, &dwBytesNeeded)) {
    dwError = GetLastError();
    if (ERROR_INSUFFICIENT_BUFFER == dwError) {
      cbBufSize = dwBytesNeeded;
      lpsc = (LPQUERY_SERVICE_CONFIG)LocalAlloc(LMEM_FIXED, cbBufSize);
    } else {
      printf("QueryServiceConfig failed (%d)", dwError);
      goto cleanup;
    }
  }

  if (!QueryServiceConfig(schService, lpsc, cbBufSize, &dwBytesNeeded)) {
    printf("QueryServiceConfig failed (%d)", GetLastError());
    goto cleanup;
  }

  if (!QueryServiceConfig2(schService, SERVICE_CONFIG_DESCRIPTION, NULL, 0,
                           &dwBytesNeeded)) {
    dwError = GetLastError();
    if (ERROR_INSUFFICIENT_BUFFER == dwError) {
      cbBufSize = dwBytesNeeded;
      lpsd = (LPSERVICE_DESCRIPTION)LocalAlloc(LMEM_FIXED, cbBufSize);
    } else {
      printf("QueryServiceConfig2 failed (%d)", dwError);
      goto cleanup;
    }
  }

  if (!QueryServiceConfig2(schService, SERVICE_CONFIG_DESCRIPTION, (LPBYTE)lpsd,
                           cbBufSize, &dwBytesNeeded)) {
    printf("QueryServiceConfig2 failed (%d)", GetLastError());
    goto cleanup;
  }

  // Print the configuration information.

  _tprintf(TEXT("%s configuration: \n"), szSvcName);
  _tprintf(TEXT("  Type: 0x%x\n"), lpsc->dwServiceType);
  _tprintf(TEXT("  Start Type: 0x%x\n"), lpsc->dwStartType);
  _tprintf(TEXT("  Error Control: 0x%x\n"), lpsc->dwErrorControl);
  _tprintf(TEXT("  Binary path: %s\n"), lpsc->lpBinaryPathName);
  _tprintf(TEXT("  Account: %s\n"), lpsc->lpServiceStartName);

  if (lpsd->lpDescription != NULL &&
      lstrcmp(lpsd->lpDescription, TEXT("")) != 0)
    _tprintf(TEXT("  Description: %s\n"), lpsd->lpDescription);
  if (lpsc->lpLoadOrderGroup != NULL &&
      lstrcmp(lpsc->lpLoadOrderGroup, TEXT("")) != 0)
    _tprintf(TEXT("  Load order group: %s\n"), lpsc->lpLoadOrderGroup);
  if (lpsc->dwTagId != 0)
    _tprintf(TEXT("  Tag ID: %d\n"), lpsc->dwTagId);
  if (lpsc->lpDependencies != NULL &&
      lstrcmp(lpsc->lpDependencies, TEXT("")) != 0)
    _tprintf(TEXT("  Dependencies: %s\n"), lpsc->lpDependencies);

  LocalFree(lpsc);
  LocalFree(lpsd);

cleanup:
  CloseServiceHandle(schService);
  CloseServiceHandle(schSCManager);
}

//
// Purpose:
//   Disables the service.
//
// Parameters:
//   None
//
// Return value:
//   None
//
VOID __stdcall DoDisableSvc() {
  SC_HANDLE schSCManager;
  SC_HANDLE schService;

  // Get a handle to the SCM database.

  schSCManager = OpenSCManager(NULL, // local computer
                               NULL, // ServicesActive database
                               SC_MANAGER_ALL_ACCESS); // full access rights

  if (NULL == schSCManager) {
    printf("OpenSCManager failed (%d)\n", GetLastError());
    return;
  }

  // Get a handle to the service.

  schService = OpenService(schSCManager,           // SCM database
                           szSvcName,              // name of service
                           SERVICE_CHANGE_CONFIG); // need change config access

  if (schService == NULL) {
    printf("OpenService failed (%d)\n", GetLastError());
    CloseServiceHandle(schSCManager);
    return;
  }

  // Change the service start type.

  if (!ChangeServiceConfig(schService,        // handle of service
                           SERVICE_NO_CHANGE, // service type: no change
                           SERVICE_DISABLED,  // service start type
                           SERVICE_NO_CHANGE, // error control: no change
                           NULL,              // binary path: no change
                           NULL,              // load order group: no change
                           NULL,              // tag ID: no change
                           NULL,              // dependencies: no change
                           NULL,              // account name: no change
                           NULL,              // password: no change
                           NULL))             // display name: no change
  {
    printf("ChangeServiceConfig failed (%d)\n", GetLastError());
  } else
    printf("Service disabled successfully.\n");

  CloseServiceHandle(schService);
  CloseServiceHandle(schSCManager);
}

//
// Purpose:
//   Enables the service.
//
// Parameters:
//   None
//
// Return value:
//   None
//
VOID __stdcall DoEnableSvc() {
  SC_HANDLE schSCManager;
  SC_HANDLE schService;

  // Get a handle to the SCM database.

  schSCManager = OpenSCManager(NULL, // local computer
                               NULL, // ServicesActive database
                               SC_MANAGER_ALL_ACCESS); // full access rights

  if (NULL == schSCManager) {
    printf("OpenSCManager failed (%d)\n", GetLastError());
    return;
  }

  // Get a handle to the service.

  schService = OpenService(schSCManager,           // SCM database
                           szSvcName,              // name of service
                           SERVICE_CHANGE_CONFIG); // need change config access

  if (schService == NULL) {
    printf("OpenService failed (%d)\n", GetLastError());
    CloseServiceHandle(schSCManager);
    return;
  }

  // Change the service start type.

  if (!ChangeServiceConfig(schService,           // handle of service
                           SERVICE_NO_CHANGE,    // service type: no change
                           SERVICE_AUTO_START,   // service start type
                           SERVICE_NO_CHANGE,    // error control: no change
                           NULL,                 // binary path: no change
                           NULL,                 // load order group: no change
                           NULL,                 // tag ID: no change
                           NULL,                 // dependencies: no change
                           NULL,                 // account name: no change
                           NULL,                 // password: no change
                           NULL))                // display name: no change
  {
    printf("ChangeServiceConfig failed (%d)\n", GetLastError());
  } else
    printf("Service enabled successfully.\n");

  CloseServiceHandle(schService);
  CloseServiceHandle(schSCManager);
}

//
// Purpose:
//   Updates the service description to "This is a test description".
//
// Parameters:
//   None
//
// Return value:
//   None
//
VOID __stdcall DoUpdateSvcDesc() {
  SC_HANDLE schSCManager;
  SC_HANDLE schService;
  SERVICE_DESCRIPTION sd;
  LPTSTR szDesc = TEXT("This is a test description");

  // Get a handle to the SCM database.

  schSCManager = OpenSCManager(NULL, // local computer
                               NULL, // ServicesActive database
                               SC_MANAGER_ALL_ACCESS); // full access rights

  if (NULL == schSCManager) {
    printf("OpenSCManager failed (%d)\n", GetLastError());
    return;
  }

  // Get a handle to the service.

  schService = OpenService(schSCManager,           // SCM database
                           szSvcName,              // name of service
                           SERVICE_CHANGE_CONFIG); // need change config access

  if (schService == NULL) {
    printf("OpenService failed (%d)\n", GetLastError());
    CloseServiceHandle(schSCManager);
    return;
  }

  // Change the service description.

  sd.lpDescription = szDesc;

  if (!ChangeServiceConfig2(schService,                 // handle to service
                            SERVICE_CONFIG_DESCRIPTION, // change: description
                            &sd))                       // new description
  {
    printf("ChangeServiceConfig2 failed\n");
  } else
    printf("Service description updated successfully.\n");

  CloseServiceHandle(schService);
  CloseServiceHandle(schSCManager);
}

//
// Purpose:
//   Deletes a service from the SCM database
//
// Parameters:
//   None
//
// Return value:
//   None
//
VOID __stdcall DoDeleteSvc() {
  SC_HANDLE schSCManager;
  SC_HANDLE schService;
  SERVICE_STATUS ssStatus;

  // Get a handle to the SCM database.

  schSCManager = OpenSCManager(NULL, // local computer
                               NULL, // ServicesActive database
                               SC_MANAGER_ALL_ACCESS); // full access rights

  if (NULL == schSCManager) {
    printf("OpenSCManager failed (%d)\n", GetLastError());
    return;
  }

  // Get a handle to the service.

  schService = OpenService(schSCManager, // SCM database
                           szSvcName,    // name of service
                           DELETE);      // need delete access

  if (schService == NULL) {
    printf("OpenService failed (%d)\n", GetLastError());
    CloseServiceHandle(schSCManager);
    return;
  }

  // Delete the service.

  if (!DeleteService(schService)) {
    printf("DeleteService failed (%d)\n", GetLastError());
  } else
    printf("Service deleted successfully\n");

  CloseServiceHandle(schService);
  CloseServiceHandle(schSCManager);
}
