/// macOS has a specific requirement that there must be a run loop running
/// on the main thread in order to open windows and use OpenGL.

#[cfg(target_os = "macos")]
mod runloop {
    use std::os::raw::c_void;
    #[repr(C)]
    pub struct CFRunLoop(*mut c_void);

    #[link(name = "foundation", kind = "framework")]
    extern "C" {
        fn CFRunLoopRun();
        fn CFRunLoopGetMain() -> *mut c_void;
        fn CFRunLoopStop(l: *mut c_void);
    }

    impl CFRunLoop {
        pub fn run() {
            unsafe {
                CFRunLoopRun();
            }
        }

        pub fn get_main() -> CFRunLoop {
            unsafe {
                let r = CFRunLoopGetMain();
                assert!(!r.is_null());
                CFRunLoop(r)
            }
        }

        pub fn stop(&self) {
            unsafe { CFRunLoopStop(self.0) }
        }
    }

    unsafe impl Send for CFRunLoop {}
}

/// On macOS this launches the callback function on a thread.
/// On other platforms it's just executed immediately.
#[cfg(not(target_os = "macos"))]
pub fn run<T, F: FnOnce() -> T + Send + 'static>(main: F) -> T
where
    T: Send + 'static,
{
    main()
}

#[cfg(target_os = "macos")]
pub fn run<T, F: FnOnce() -> T + Send + 'static>(main: F) -> T
where
    T: Send + 'static,
{
    use std::thread;

    let l = runloop::CFRunLoop::get_main();
    let t = thread::spawn(move || {
        let res = main();
        l.stop();
        res
    });

    runloop::CFRunLoop::run();

    t.join().unwrap()
}
