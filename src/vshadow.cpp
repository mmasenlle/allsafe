
#include <windows.h>
#include <winbase.h>

// VSS includes
#define VSS_RESTORE_TYPE unsigned
#include <vss.h>

#include <boost/scoped_ptr.hpp>
#include "log_sched.h"

//void print_vssid(const VSS_ID &vssid)
//{
//    std::cout << "vssid: " << std::hex << vssid.Data1 << "." << std::hex << vssid.Data2 << "." << std::hex << vssid.Data3;
//    for (int i=0; i < 8; i++) std::cout << "." << std::hex << (int)vssid.Data4[i]; std::cout << std::endl;
//}



static bool bCoInitializeCalled = false;
static VSS_ID           GUID_NULL0;

static void WaitAndCheckForAsyncOperation(IVssAsync *pAsync)
{
    HRESULT hrInternal = pAsync->Wait(100000);
    if (FAILED(hrInternal)) {
        ELOG << "WaitAndCheckForAsyncOperation::pAsync->Wait() " << hrInternal;
    }
    // Check the result of the asynchronous operation
    HRESULT hrReturned = S_OK;
    hrInternal = pAsync->QueryStatus(&hrReturned, NULL);
    if (FAILED(hrInternal)) {
        ELOG << "WaitAndCheckForAsyncOperation::pAsync->QueryStatus() " << hrInternal;
    }
    // Check if the async operation succeeded...
    if(FAILED(hrReturned)) {
        ELOG << "WaitAndCheckForAsyncOperation::pAsync->QueryStatus()->hrReturned: " << hrReturned;
    }
}

std::string vshadow_create(char drive)
{
    std::string vs_path;
    if (!bCoInitializeCalled) {
        HRESULT hrInternal = CoInitialize(NULL);
        if (FAILED(hrInternal)) {
            ELOG << "vshadow_create("<<drive<<")->CoInitialize " << hrInternal;
        }
        // Initialize COM security
        hrInternal = CoInitializeSecurity(
                NULL,                           //  Allow *all* VSS writers to communicate back!
                -1,                             //  Default COM authentication service
                NULL,                           //  Default COM authorization service
                NULL,                           //  reserved parameter
                RPC_C_AUTHN_LEVEL_PKT_PRIVACY,  //  Strongest COM authentication level
                RPC_C_IMP_LEVEL_IMPERSONATE,    //  Minimal impersonation abilities
                NULL,                           //  Default COM authentication settings
                EOAC_DYNAMIC_CLOAKING,          //  Cloaking
                NULL                            //  Reserved parameter
                );

        if (FAILED(hrInternal)) {
            ELOG << "vshadow_create("<<drive<<")->CoInitializeSecurity " << hrInternal;
            return vs_path;
        }
    }
    bCoInitializeCalled = true;
    IVssBackupComponents  *m_pVssObject = NULL;
    HRESULT hrInternal = CreateVssBackupComponents(&m_pVssObject);
    boost::scoped_ptr<IVssBackupComponents> scoped_pVssObject(m_pVssObject);
    if (FAILED(hrInternal)) {
        ELOG << "vshadow_create("<<drive<<")->CreateVssBackupComponents " << hrInternal;
        return vs_path;
    }
    hrInternal = m_pVssObject->InitializeForBackup(0);
    if (FAILED(hrInternal)) {
        ELOG << "vshadow_create("<<drive<<")->InitializeForBackup " << hrInternal;
    }
    hrInternal = m_pVssObject->SetBackupState(true, true, VSS_BT_FULL, false);
    if (FAILED(hrInternal)) {
        ELOG << "vshadow_create("<<drive<<")->SetBackupState " << hrInternal;
    }
    VSS_ID latestSnapshotSetID;// = GUID_NULL;
    hrInternal = m_pVssObject->StartSnapshotSet(&latestSnapshotSetID);
    if (FAILED(hrInternal)) {
        ELOG << "vshadow_create("<<drive<<")->StartSnapshotSet " << hrInternal;
    }
    wchar_t buffer1[MAX_PATH],buffer2[MAX_PATH],wdrive[8] = L"C:\\";
    wdrive[0] = (wchar_t)drive;
    hrInternal = GetVolumeNameForVolumeMountPointW(wdrive, buffer1, MAX_PATH);
    if (FAILED(hrInternal)) {
        ELOG << "vshadow_create("<<drive<<")->GetVolumeNameForVolumeMountPointW 1 " << hrInternal;
    }
    hrInternal = GetVolumeNameForVolumeMountPointW(buffer1, buffer2, MAX_PATH);
    if (FAILED(hrInternal)) {
        ELOG << "vshadow_create("<<drive<<")->GetVolumeNameForVolumeMountPointW 2 " << hrInternal;
    }
    VSS_ID SnapshotID; memset(&SnapshotID, 0, sizeof(SnapshotID));
    hrInternal = m_pVssObject->AddToSnapshotSet(buffer2, GUID_NULL0, &SnapshotID);
    if (FAILED(hrInternal)) {
        ELOG << "vshadow_create("<<drive<<")->AddToSnapshotSet " << hrInternal;
    }
    IVssAsync *pAsync = NULL;
    hrInternal = m_pVssObject->PrepareForBackup(&pAsync);
    if (FAILED(hrInternal)) {
        ELOG << "vshadow_create("<<drive<<")->DoSnapshotSet " << hrInternal;
    } else if (pAsync) WaitAndCheckForAsyncOperation(pAsync);
    delete pAsync; pAsync = NULL;
    hrInternal = m_pVssObject->DoSnapshotSet(&pAsync);
    if (FAILED(hrInternal)) {
        ELOG << "vshadow_create("<<drive<<")->DoSnapshotSet " << hrInternal;
    } else if (pAsync) WaitAndCheckForAsyncOperation(pAsync);
    delete pAsync; pAsync = NULL;
    IVssEnumObject *pIEnumSnapshots;
    HRESULT hr = m_pVssObject->Query( GUID_NULL0,
            VSS_OBJECT_NONE,
            VSS_OBJECT_SNAPSHOT,
            &pIEnumSnapshots );
    if (FAILED(hr)) {
        ELOG << "vshadow_create("<<drive<<")->Query " << hr;
        return vs_path;
    }
    VSS_OBJECT_PROP Prop;
    VSS_SNAPSHOT_PROP& Snap = Prop.Obj.Snap;
    while(true)
    {
        // Get the next element
        ULONG ulFetched = 0;
        hr = pIEnumSnapshots->Next( 1, &Prop, &ulFetched );
        if (FAILED(hr)) {
            ELOG << "vshadow_create("<<drive<<")->pIEnumSnapshots->Next " << hr;
        }

        // We reached the end of list
        if (ulFetched == 0)
            break;

        //print_vssid(Prop.Obj.Snap.m_SnapshotSetId);
        //std::wcout << L"m_pwszSnapshotDeviceObject: " << Prop.Obj.Snap.m_pwszSnapshotDeviceObject << std::endl;
        //std::wcout << L"m_pwszOriginalVolumeName: " << Prop.Obj.Snap.m_pwszOriginalVolumeName << std::endl;

        if (memcmp(&latestSnapshotSetID, &Prop.Obj.Snap.m_SnapshotSetId, sizeof(latestSnapshotSetID)) == 0) {
            for (int i = 0; Prop.Obj.Snap.m_pwszSnapshotDeviceObject[i]; i++) vs_path.push_back((char)Prop.Obj.Snap.m_pwszSnapshotDeviceObject[i]);
            return vs_path;
        }

    }

    return vs_path;
}


#ifndef NDEBUG
#include <boost/test/unit_test.hpp>

#include <fstream>
#include <boost/filesystem.hpp>

BOOST_AUTO_TEST_SUITE (main_test_suite_vshadow)

BOOST_AUTO_TEST_CASE (vshadow1)
{
    std::string vs_path = vshadow_create('C');
    BOOST_REQUIRE( vs_path.empty() == false );
    { std::string fname = "C:\\prueba1\\LICENSE";
        std::string vfname = vs_path + fname.substr(2);
    std::ifstream ifs(vfname.c_str(), std::ifstream::binary);
    std::ofstream ofs((fname + ".shdw").c_str(), std::ifstream::binary);
    char data[4096]; ifs.read(data, 4096);
    while (ifs.gcount()) {ofs.write(data, ifs.gcount()); ifs.read(data, 4096);}
    BOOST_CHECK( boost::filesystem::is_regular_file(fname + ".shdw") &&
        (boost::filesystem::file_size(fname) == boost::filesystem::file_size(fname + ".shdw")) );
    }
}
BOOST_AUTO_TEST_CASE (vshadow2)
{
    std::string vs_path = vshadow_create('E');
    BOOST_REQUIRE( vs_path.empty() == false );
    { std::string fname = "E:\\tmp\\LICENSE";
        std::string vfname = vs_path + fname.substr(2);
        boost::filesystem::copy_file(vfname, fname + ".shdw", boost::filesystem::copy_option::overwrite_if_exists);
        BOOST_CHECK( boost::filesystem::is_regular_file(fname + ".shdw") &&
                    (boost::filesystem::file_size(fname) == boost::filesystem::file_size(fname + ".shdw")) );
//    std::ifstream ifs(fname.c_str(), std::ifstream::binary);
//    std::ofstream ofs((fname + ".shdw").c_str(), std::ifstream::binary);
//    char data[4096]; ifs.read(data, 4096);
//    while (ifs.gcount()) {ofs.write(data, ifs.gcount()); ifs.read(data, 4096);}
    }
}

BOOST_AUTO_TEST_SUITE_END( )

#endif


