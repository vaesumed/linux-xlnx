extern void Daemon_Added_Resource(void);
extern void Daemon_Close_Control(void);
extern void Daemon_CreateSessionId(void);
extern void Daemon_DestroySessionId(void);
extern void Daemon_Dumpque(void);
extern void Daemon_Get_UserSpace(void);
extern void Daemon_Library_close(void);
extern void Daemon_Library_ioctl(void);
extern void Daemon_Library_open(void);
extern void Daemon_Library_read(void);
extern void Daemon_Library_write(void);
extern void Daemon_Login(void);
extern void Daemon_Logout(void);
extern void Daemon_Open_Control(void);
extern void Daemon_Poll(void);
extern void Daemon_Receive_Reply(void);
extern void Daemon_Remove_Resource(void);
extern void Daemon_Send_Command(void);
extern void Daemon_SetMountPoint(void);
extern void Daemon_getpwuid(void);
extern void Daemon_getversion(void);
extern void Daemon_ioctl(void);
extern void GetConnData(void);
extern void GetUserData(void);
extern void Init_Daemon_Queue(void);
extern void Init_Procfs_Interface(void);
extern void Novfs_Add_to_Root(void);
extern void Novfs_Add_to_Root2(void);
extern void Novfs_Close_File(void);
extern void Novfs_Close_Stream(void);
extern void Novfs_Control_ioctl(void);
extern void Novfs_Control_read(void);
extern void Novfs_Control_write(void);
extern void Novfs_Create(void);
extern void Novfs_Delete(void);
extern void Novfs_Find_Name_In_List(void);
extern void Novfs_Get_Connected_Server_List(void);
extern void Novfs_Get_Directory_List(void);
extern void Novfs_Get_Directory_ListEx(void);
extern void Novfs_Get_File_Info(void);
extern void Novfs_Get_File_Info2(void);
extern void Novfs_Get_Server_Volume_List(void);
extern void Novfs_Get_Version(void);
extern void Novfs_Open_File(void);
extern void Novfs_Read_File(void);
extern void Novfs_Read_Stream(void);
extern void Novfs_Remove_from_Root(void);
extern void Novfs_Rename_File(void);
extern void Novfs_Set_Attr(void);
extern void Novfs_Truncate_File(void);
extern void Novfs_User_proc_ioctl(void);
extern void Novfs_User_proc_read(void);
extern void Novfs_User_proc_write(void);
extern void Novfs_Verify_Server_Name(void);
extern void Novfs_Verify_Volume_Name(void);
extern void Novfs_Write_File(void);
extern void Novfs_Write_Stream(void);
extern void Novfs_a_readpage(void);
extern void Novfs_add_inode_entry(void);
extern void Novfs_clear_inode(void);
extern void Novfs_d_add(void);
extern void Novfs_d_compare(void);
extern void Novfs_d_delete(void);
extern void Novfs_d_hash(void);
extern void Novfs_d_iput(void);
extern void Novfs_d_lookup(void);
extern void Novfs_d_release(void);
extern void Novfs_d_revalidate(void);
extern void Novfs_d_strcmp(void);
extern void Novfs_dget_path(void);
extern void Novfs_dir_fsync(void);
extern void Novfs_dir_lseek(void);
extern void Novfs_dir_open(void);
extern void Novfs_dir_read(void);
extern void Novfs_dir_readdir(void);
extern void Novfs_dir_release(void);
extern void Novfs_enumerate_inode_cache(void);
extern void Novfs_f_flush(void);
extern void Novfs_f_fsync(void);
extern void Novfs_f_ioctl(void);
extern void Novfs_f_llseek(void);
extern void Novfs_f_lock(void);
extern void Novfs_f_mmap(void);
extern void Novfs_f_open(void);
extern void Novfs_f_read(void);
extern void Novfs_f_readdir(void);
extern void Novfs_f_release(void);
extern void Novfs_f_write(void);
extern void Novfs_fill_super(void);
extern void Novfs_free_inode_cache(void);
extern void Novfs_free_invalid_entries(void);
extern void Novfs_get_alltrees(void);
extern void Novfs_get_entry(void);
extern void Novfs_get_entry_time(void);
extern void Novfs_get_inode(void);
extern void Novfs_get_remove_entry(void);
extern void Novfs_get_sb(void);
extern void Novfs_i_create(void);
extern void Novfs_i_getattr(void);
extern void Novfs_i_lookup(void);
extern void Novfs_i_mkdir(void);
extern void Novfs_i_mknod(void);
extern void Novfs_i_permission(void);
extern void Novfs_i_rename(void);
extern void Novfs_i_revalidate(void);
extern void Novfs_i_rmdir(void);
extern void Novfs_i_setattr(void);
extern void Novfs_i_unlink(void);
extern void Novfs_internal_hash(void);
extern void Novfs_invalidate_inode_cache(void);
extern void Novfs_kill_sb(void);
extern void Novfs_lock_inode_cache(void);
extern void Novfs_lookup_inode_cache(void);
extern void Novfs_lookup_validate(void);
extern void Novfs_notify_change(void);
extern void Novfs_read_inode(void);
extern void Novfs_remove_inode_entry(void);
extern void Novfs_show_options(void);
extern void Novfs_statfs(void);
extern void Novfs_tree_read(void);
extern void Novfs_unlock_inode_cache(void);
extern void Novfs_update_entry(void);
extern void Novfs_verify_file(void);
extern void Novfs_write_inode(void);
extern void NwAuthConnWithId(void);
extern void NwConnClose(void);
extern void NwGetConnInfo(void);
extern void NwGetDaemonVersion(void);
extern void NwGetIdentityInfo(void);
extern void NwLicenseConn(void);
extern void NwLoginIdentity(void);
extern void NwLogoutIdentity(void);
extern void NwOpenConnByAddr(void);
extern void NwOpenConnByName(void);
extern void NwOpenConnByRef(void);
extern void NwQueryFeature(void);
extern void NwRawSend(void);
extern void NwScanConnInfo(void);
extern void NwSetConnInfo(void);
extern void NwSysConnClose(void);
extern void NwUnAuthenticate(void);
extern void NwUnlicenseConn(void);
extern void NwcChangeAuthKey(void);
extern void NwcEnumIdentities(void);
extern void NwcEnumerateDrives(void);
extern void NwcGetBroadcastMessage(void);
extern void NwcGetDefaultNameCtx(void);
extern void NwcGetPreferredDSTree(void);
extern void NwcGetPrimaryConn(void);
extern void NwcGetTreeMonitoredConn(void);
extern void NwcSetDefaultNameCtx(void);
extern void NwcSetMapDrive(void);
extern void NwcSetPreferredDSTree(void);
extern void NwcSetPrimaryConn(void);
extern void NwcUnMapDrive(void);
extern void NwdConvertLocalHandle(void);
extern void NwdConvertNetwareHandle(void);
extern void NwdGetMountPath(void);
extern void NwdSetKeyValue(void);
extern void NwdSetMapDrive(void);
extern void NwdUnMapDrive(void);
extern void NwdVerifyKeyValue(void);
extern void Queue_Daemon_Command(void);
extern void Queue_get(void);
extern void Queue_put(void);
extern void RemoveDriveMaps(void);
extern void Scope_Cleanup(void);
extern void Scope_Cleanup_Thread(void);
extern void Scope_Dump_Scopetable(void);
extern void Scope_Dump_Tasklist(void);
extern void Scope_Find_Scope(void);
extern void Scope_Get_Hash(void);
extern void Scope_Get_ScopeUsers(void);
extern void Scope_Get_ScopefromName(void);
extern void Scope_Get_ScopefromPath(void);
extern void Scope_Get_SessionId(void);
extern void Scope_Get_Uid(void);
extern void Scope_Get_UserName(void);
extern void Scope_Get_UserSpace(void);
extern void Scope_Init(void);
extern void Scope_Lookup(void);
extern void Scope_Search4Scope(void);
extern void Scope_Set_UserSpace(void);
extern void Scope_Timer_Function(void);
extern void Scope_Uninit(void);
extern void Scope_Validate_Scope(void);
extern void Uninit_Daemon_Queue(void);
extern void Uninit_Procfs_Interface(void);
extern void add_to_list(void);
extern void begin_directory_enumerate(void);
extern void directory_enumerate(void);
extern void directory_enumerate_ex(void);
extern void do_login(void);
extern void do_logout(void);
extern void end_directory_enumerate(void);
extern void exit_novfs(void);
extern void find_queue(void);
extern void get_next_queue(void);
extern void init_novfs(void);
extern void local_unlink(void);
extern void process_list(void);
extern void update_inode(void);
extern void verify_dentry(void);
extern void processList(void);
extern void processEntries(void);

SYMBOL_TABLE SymbolTable[] = {
	{Scope_Get_UserSpace, "Scope_Get_UserSpace"},
	{NwLoginIdentity, "NwLoginIdentity"},
	{Novfs_d_revalidate, "Novfs_d_revalidate"},
	{Daemon_SetMountPoint, "Daemon_SetMountPoint"},
	{Scope_Get_Hash, "Scope_Get_Hash"},
	{Queue_get, "Queue_get"},
	{Queue_Daemon_Command, "Queue_Daemon_Command"},
	{Novfs_dir_fsync, "Novfs_dir_fsync"},
	{Novfs_Read_File, "Novfs_Read_File"},
	{Daemon_Library_close, "Daemon_Library_close"},
	{NwRawSend, "NwRawSend"},
	{Novfs_get_inode, "Novfs_get_inode"},
	{Novfs_Remove_from_Root, "Novfs_Remove_from_Root"},
	{Novfs_Find_Name_In_List, "Novfs_Find_Name_In_List"},
	{Scope_Get_SessionId, "Scope_Get_SessionId"},
	{NwOpenConnByAddr, "NwOpenConnByAddr"},
	{Novfs_read_inode, "Novfs_read_inode"},
	{Novfs_Truncate_File, "Novfs_Truncate_File"},
	{Daemon_Login, "Daemon_Login"},
	{Scope_Get_ScopefromPath, "Scope_Get_ScopefromPath"},
	{NwcGetTreeMonitoredConn, "NwcGetTreeMonitoredConn"},
	{Novfs_write_inode, "Novfs_write_inode"},
	{Scope_Lookup, "Scope_Lookup"},
	{NwQueryFeature, "NwQueryFeature"},
	{Novfs_get_entry_time, "Novfs_get_entry_time"},
	{Novfs_Control_write, "Novfs_Control_write"},
	{Scope_Get_Uid, "Scope_Get_Uid"},
	{NwSysConnClose, "NwSysConnClose"},
	{NwConnClose, "NwConnClose"},
	{Novfs_get_entry, "Novfs_get_entry"},
	{Novfs_Rename_File, "Novfs_Rename_File"},
	{NwdConvertLocalHandle, "NwdConvertLocalHandle"},
	{Novfs_dir_lseek, "Novfs_dir_lseek"},
	{Scope_Get_ScopefromName, "Scope_Get_ScopefromName"},
	{NwcGetPrimaryConn, "NwcGetPrimaryConn"},
	{Novfs_d_strcmp, "Novfs_d_strcmp"},
	{Daemon_Library_ioctl, "Daemon_Library_ioctl"},
	{end_directory_enumerate, "end_directory_enumerate"},
	{directory_enumerate, "directory_enumerate"},
	{begin_directory_enumerate, "begin_directory_enumerate"},
	{NwdGetMountPath, "NwdGetMountPath"},
	{NwAuthConnWithId, "NwAuthConnWithId"},
	{Novfs_Set_Attr, "Novfs_Set_Attr"},
	{Daemon_getversion, "Daemon_getversion"},
	{Scope_Dump_Scopetable, "Scope_Dump_Scopetable"},
	{NwcSetMapDrive, "NwcSetMapDrive"},
	{Novfs_lookup_inode_cache, "Novfs_lookup_inode_cache"},
	{Novfs_i_mkdir, "Novfs_i_mkdir"},
	{Novfs_free_invalid_entries, "Novfs_free_invalid_entries"},
	{Novfs_dump_inode_cache, "Novfs_dump_inode_cache"},
	{Novfs_Write_Stream, "Novfs_Write_Stream"},
	{Novfs_Verify_Server_Name, "Novfs_Verify_Server_Name"},
	{GetConnData, "GetConnData"},
	{Uninit_Procfs_Interface, "Uninit_Procfs_Interface"},
	{Scope_Validate_Scope, "Scope_Validate_Scope"},
	{Scope_Timer_Function, "Scope_Timer_Function"},
	{Novfs_i_setattr, "Novfs_i_setattr"},
	{Novfs_i_mknod, "Novfs_i_mknod"},
	{Novfs_Verify_Volume_Name, "Novfs_Verify_Volume_Name"},
	{Novfs_Close_Stream, "Novfs_Close_Stream"},
	{Novfs_Add_to_Root, "Novfs_Add_to_Root"},
	{Init_Procfs_Interface, "Init_Procfs_Interface"},
	{Novfs_dump_inode, "Novfs_dump_inode"},
	{Novfs_Get_Directory_List, "Novfs_Get_Directory_List"},
	{Novfs_Get_Connected_Server_List, "Novfs_Get_Connected_Server_List"},
	{Daemon_Logout, "Daemon_Logout"},
	{do_logout, "do_logout"},
	{Scope_Search4Scope, "Scope_Search4Scope"},
	{NwdUnMapDrive, "NwdUnMapDrive"},
	{Novfs_Control_read, "Novfs_Control_read"},
	{Scope_Cleanup_Thread, "Scope_Cleanup_Thread"},
	{Novfs_invalidate_inode_cache, "Novfs_invalidate_inode_cache"},
	{Novfs_f_flush, "Novfs_f_flush"},
	{Novfs_enumerate_inode_cache, "Novfs_enumerate_inode_cache"},
	{Novfs_d_compare, "Novfs_d_compare"},
	{Daemon_Library_write, "Daemon_Library_write"},
	{GetUserData, "GetUserData"},
	{Daemon_Remove_Resource, "Daemon_Remove_Resource"},
	{Scope_Set_UserSpace, "Scope_Set_UserSpace"},
	{Novfs_get_alltrees, "Novfs_get_alltrees"},
	{Daemon_Get_UserSpace, "Daemon_Get_UserSpace"},
	{Uninit_Daemon_Queue, "Uninit_Daemon_Queue"},
	{NwcChangeAuthKey, "NwcChangeAuthKey"},
	{NwLicenseConn, "NwLicenseConn"},
	{Init_Daemon_Queue, "Init_Daemon_Queue"},
	{Novfs_tree_read, "Novfs_tree_read"},
	{Novfs_f_llseek, "Novfs_f_llseek"},
	{find_queue, "find_queue"},
	{Scope_Find_Scope, "Scope_Find_Scope"},
	{Novfs_lookup_validate, "Novfs_lookup_validate"},
	{Novfs_d_hash, "Novfs_d_hash"},
	{Novfs_a_readpage, "Novfs_a_readpage"},
	{Novfs_Create, "Novfs_Create"},
	{Novfs_Close_File, "Novfs_Close_File"},
	{Daemon_getpwuid, "Daemon_getpwuid"},
	{Daemon_CreateSessionId, "Daemon_CreateSessionId"},
	{Scope_dget_path, "Scope_dget_path"},
	{NwcSetDefaultNameCtx, "NwcSetDefaultNameCtx"},
	{NwcGetDefaultNameCtx, "NwcGetDefaultNameCtx"},
	{NwUnAuthenticate, "NwUnAuthenticate"},
	{Novfs_i_getattr, "Novfs_i_getattr"},
	{Novfs_get_remove_entry, "Novfs_get_remove_entry"},
	{Novfs_f_ioctl, "Novfs_f_ioctl"},
	{Scope_Get_ScopeUsers, "Scope_Get_ScopeUsers"},
	{Scope_Dump_Tasklist, "Scope_Dump_Tasklist"},
	{NwOpenConnByRef, "NwOpenConnByRef"},
	{Novfs_unlock_inode_cache, "Novfs_unlock_inode_cache"},
	{Novfs_lock_inode_cache, "Novfs_lock_inode_cache"},
	{Daemon_DestroySessionId, "Daemon_DestroySessionId"},
	{do_login, "do_login"},
	{Novfs_free_inode_cache, "Novfs_free_inode_cache"},
	{Novfs_Read_Stream, "Novfs_Read_Stream"},
	{Daemon_Library_read, "Daemon_Library_read"},
	{NwdSetMapDrive, "NwdSetMapDrive"},
	{Novfs_internal_hash, "Novfs_internal_hash"},
	{Daemon_Receive_Reply, "Daemon_Receive_Reply"},
	{Daemon_Library_open, "Daemon_Library_open"},
	{get_next_queue, "get_next_queue"},
	{exit_novfs, "exit_novfs"},
	{NwcGetBroadcastMessage, "NwcGetBroadcastMessage"},
	{Novfs_d_lookup, "Novfs_d_lookup"},
	{Novfs_clear_inode, "Novfs_clear_inode"},
	{Daemon_Open_Control, "Daemon_Open_Control"},
	{NwdConvertNetwareHandle, "NwdConvertNetwareHandle"},
	{NwcUnMapDrive, "NwcUnMapDrive"},
	{Novfs_notify_change, "Novfs_notify_change"},
	{Novfs_dir_release, "Novfs_dir_release"},
	{directory_enumerate_ex, "directory_enumerate_ex"},
	{RemoveDriveMaps, "RemoveDriveMaps"},
	{NwOpenConnByName, "NwOpenConnByName"},
	{Novfs_verify_file, "Novfs_verify_file"},
	{Novfs_statfs, "Novfs_statfs"},
	{Novfs_f_write, "Novfs_f_write"},
	{Novfs_Get_File_Info, "Novfs_Get_File_Info"},
	{Novfs_Delete, "Novfs_Delete"},
	{update_inode, "update_inode"},
	{NwcSetPreferredDSTree, "NwcSetPreferredDSTree"},
	{NwcGetPreferredDSTree, "NwcGetPreferredDSTree"},
	{Novfs_update_entry, "Novfs_update_entry"},
	{Novfs_kill_sb, "Novfs_kill_sb"},
	{Daemon_ioctl, "Daemon_ioctl"},
	{Scope_Get_UserName, "Scope_Get_UserName"},
	{NwcEnumerateDrives, "NwcEnumerateDrives"},
	{Novfs_i_revalidate, "Novfs_i_revalidate"},
	{Novfs_f_release, "Novfs_f_release"},
	{Novfs_f_read, "Novfs_f_read"},
	{Novfs_d_delete, "Novfs_d_delete"},
	{Novfs_Write_File, "Novfs_Write_File"},
	{Novfs_User_proc_ioctl, "Novfs_User_proc_ioctl"},
	{Novfs_Get_File_Info2, "Novfs_Get_File_Info2"},
	{NwdSetKeyValue, "NwdSetKeyValue"},
	{Novfs_remove_inode_entry, "Novfs_remove_inode_entry"},
	{Novfs_i_rename, "Novfs_i_rename"},
	{Novfs_f_open, "Novfs_f_open"},
	{Novfs_d_iput, "Novfs_d_iput"},
	{Novfs_Get_Directory_ListEx, "Novfs_Get_Directory_ListEx"},
	{Daemon_Close_Control, "Daemon_Close_Control"},
	{verify_dentry, "verify_dentry"},
	{local_unlink, "local_unlink"},
	{init_novfs, "init_novfs"},
	{NwUnlicenseConn, "NwUnlicenseConn"},
	{NwGetConnInfo, "NwGetConnInfo"},
	{Novfs_i_permission, "Novfs_i_permission"},
	{Novfs_dir_read, "Novfs_dir_read"},
	{NwcSetPrimaryConn, "NwcSetPrimaryConn"},
	{Novfs_f_lock, "Novfs_f_lock"},
	{Novfs_dir_readdir, "Novfs_dir_readdir"},
	{Novfs_dir_open, "Novfs_dir_open"},
	{Queue_put, "Queue_put"},
	{NwLogoutIdentity, "NwLogoutIdentity"},
	{NwGetIdentityInfo, "NwGetIdentityInfo"},
	{Novfs_i_rmdir, "Novfs_i_rmdir"},
	{Novfs_i_create, "Novfs_i_create"},
	{Novfs_f_mmap, "Novfs_f_mmap"},
	{Novfs_User_proc_read, "Novfs_User_proc_read"},
	{Novfs_show_options, "Novfs_show_options"},
	{Novfs_add_inode_entry, "Novfs_add_inode_entry"},
	{Novfs_Open_File, "Novfs_Open_File"},
	{Novfs_Get_Version, "Novfs_Get_Version"},
	{Daemon_Poll, "Daemon_Poll"},
	{add_to_list, "add_to_list"},
	{Scope_Init, "Scope_Init"},
	{Scope_Cleanup, "Scope_Cleanup"},
	{NwSetConnInfo, "NwSetConnInfo"},
	{Novfs_i_unlink, "Novfs_i_unlink"},
	{Novfs_get_sb, "Novfs_get_sb"},
	{Novfs_f_readdir, "Novfs_f_readdir"},
	{Novfs_f_fsync, "Novfs_f_fsync"},
	{Novfs_d_release, "Novfs_d_release"},
	{Novfs_User_proc_write, "Novfs_User_proc_write"},
	{Daemon_Send_Command, "Daemon_Send_Command"},
	{Daemon_Dumpque, "Daemon_Dumpque"},
	{NwcEnumIdentities, "NwcEnumIdentities"},
	{NwGetDaemonVersion, "NwGetDaemonVersion"},
	{Novfs_i_lookup, "Novfs_i_lookup"},
	{Novfs_fill_super, "Novfs_fill_super"},
	{Novfs_Get_Server_Volume_List, "Novfs_Get_Server_Volume_List"},
	{Novfs_Add_to_Root2, "Novfs_Add_to_Root2"},
	{Daemon_SendDebugCmd, "Daemon_SendDebugCmd"},
	{Daemon_Added_Resource, "Daemon_Added_Resource"},
	{Scope_Uninit, "Scope_Uninit"},
	{NwdVerifyKeyValue, "NwdVerifyKeyValue"},
	{NwScanConnInfo, "NwScanConnInfo"},
	{Novfs_dget_path, "Novfs_dget_path"},
	{Novfs_d_add, "Novfs_d_add"},
	{Novfs_Control_ioctl, "Novfs_Control_ioctl"},
	{processList, "processList"},
	{processEntries, "processEntries"},

	// Terminate the table
	{NULL, NULL}
};
