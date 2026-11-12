//! A shared memory block the viewer made (POSIX `shm_open` "/hwvx-...", Windows "Local\hwvx-..."),
//! mapped read-write by name. The name is unlinked once mapped (POSIX): no one else opens it, and
//! it goes with the last mapping. Used by the frames (`frames.rs`) and the media blobs (`media.rs`).

pub struct Mapping {
    pub base: *mut u8,
    pub size: usize,
    pub name: String,
    #[cfg(windows)]
    handle: windows_sys::Win32::Foundation::HANDLE,
}

// only touched through atomics, volatile reads and copies
unsafe impl Send for Mapping {}

impl Mapping {
    #[cfg(unix)]
    pub fn open(name: &str, size: usize) -> Result<Mapping, String> {
        let cname = std::ffi::CString::new(name).map_err(|_| "bad name".to_string())?;
        unsafe {
            let fd = libc::shm_open(cname.as_ptr(), libc::O_RDWR, 0);
            if fd < 0 {
                return Err(format!("{name}: {}", std::io::Error::last_os_error()));
            }
            let mut st: libc::stat = std::mem::zeroed();
            if libc::fstat(fd, &mut st) != 0 || (st.st_size as usize) < size {
                libc::close(fd);
                return Err(format!("{name}: smaller than {size} bytes"));
            }
            let p = libc::mmap(std::ptr::null_mut(), size, libc::PROT_READ | libc::PROT_WRITE, libc::MAP_SHARED, fd, 0);
            libc::close(fd);
            // the name has done its job: no one else is to open it, and it goes with the last mapping
            libc::shm_unlink(cname.as_ptr());
            if p == libc::MAP_FAILED {
                return Err(format!("{name}: {}", std::io::Error::last_os_error()));
            }
            Ok(Mapping { base: p as *mut u8, size, name: name.to_string() })
        }
    }

    #[cfg(windows)]
    pub fn open(name: &str, size: usize) -> Result<Mapping, String> {
        use windows_sys::Win32::Foundation::CloseHandle;
        use windows_sys::Win32::System::Memory::{FILE_MAP_ALL_ACCESS, MapViewOfFile, OpenFileMappingW};
        let wide: Vec<u16> = name.encode_utf16().chain(std::iter::once(0)).collect();
        unsafe {
            let h = OpenFileMappingW(FILE_MAP_ALL_ACCESS, 0, wide.as_ptr());
            if h.is_null() {
                return Err(format!("{name}: {}", std::io::Error::last_os_error()));
            }
            let v = MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, size);
            if v.Value.is_null() {
                CloseHandle(h);
                return Err(format!("{name}: {}", std::io::Error::last_os_error()));
            }
            Ok(Mapping { base: v.Value as *mut u8, size, name: name.to_string(), handle: h })
        }
    }
}

impl Drop for Mapping {
    fn drop(&mut self) {
        unsafe {
            #[cfg(unix)]
            libc::munmap(self.base as *mut libc::c_void, self.size);
            #[cfg(windows)]
            {
                use windows_sys::Win32::System::Memory::{MEMORY_MAPPED_VIEW_ADDRESS, UnmapViewOfFile};
                UnmapViewOfFile(MEMORY_MAPPED_VIEW_ADDRESS { Value: self.base as *mut _ });
                windows_sys::Win32::Foundation::CloseHandle(self.handle);
            }
        }
    }
}
