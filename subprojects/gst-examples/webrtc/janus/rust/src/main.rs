// GStreamer
//
// Copyright (C) 2019 Sebastian Dröge <sebastian@centricular.com>
// Copyright (C) 2020 Philippe Normand <philn@igalia.com>
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Library General Public
// License as published by the Free Software Foundation; either
// version 2 of the License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Library General Public License for more details.
//
// You should have received a copy of the GNU Library General Public
// License along with this library; if not, write to the
// Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
// Boston, MA 02110-1301, USA.

use anyhow::bail;
use gst::prelude::*;
use std::sync::{Arc, Mutex, Weak};

mod janus;

// Strong reference to our application state
#[derive(Debug, Clone)]
struct App(Arc<AppInner>);

// Weak reference to our application state
#[derive(Debug, Clone)]
struct AppWeak(Weak<AppInner>);

// Actual application state
#[derive(Debug)]
struct AppInner {
    pipeline: gst::Pipeline,
    bus_watch: Mutex<Option<gst::bus::BusWatchGuard>>,
}

// To be able to access the App's fields directly
impl std::ops::Deref for App {
    type Target = AppInner;

    fn deref(&self) -> &AppInner {
        &self.0
    }
}

impl AppWeak {
    // Try upgrading a weak reference to a strong one
    fn upgrade(&self) -> Option<App> {
        self.0.upgrade().map(App)
    }
}

impl App {
    // Downgrade the strong reference to a weak reference
    fn downgrade(&self) -> AppWeak {
        AppWeak(Arc::downgrade(&self.0))
    }

    fn new() -> Result<Self, anyhow::Error> {
        let pipeline = gst::parse_launch(
            "webrtcbin name=webrtcbin stun-server=stun://stun.l.google.com:19302 \
             videotestsrc pattern=ball ! videoconvert ! queue name=vqueue",
        )?;

        let pipeline = pipeline
            .downcast::<gst::Pipeline>()
            .expect("Couldn't downcast pipeline");

        let bus = pipeline.bus().unwrap();
        let app = App(Arc::new(AppInner {
            pipeline,
            bus_watch: Mutex::default(),
        }));

        let app_weak = app.downgrade();
        let bus_watch = bus
            .add_watch_local(move |_bus, msg| {
                let app = upgrade_weak!(app_weak, glib::ControlFlow::Break);

                if app.handle_pipeline_message(msg).is_err() {
                    return glib::ControlFlow::Break;
                }
                glib::ControlFlow::Continue
            })
            .expect("Unable to add bus watch");
        *app.0.bus_watch.lock().unwrap() = Some(bus_watch);

        Ok(app)
    }

    fn handle_pipeline_message(&self, message: &gst::Message) -> Result<(), anyhow::Error> {
        use gst::message::MessageView;

        match message.view() {
            MessageView::Error(err) => bail!(
                "Error from element {}: {} ({})",
                err.src()
                    .map(|s| String::from(s.path_string()))
                    .unwrap_or_else(|| String::from("None")),
                err.error(),
                err.debug().unwrap_or_else(|| glib::GString::from("None")),
            ),
            MessageView::Warning(warning) => {
                println!("Warning: \"{}\"", warning.debug().unwrap());
            }
            MessageView::Latency(_) => {
                let _ = self.pipeline.recalculate_latency();
            }
            _ => (),
        }
        Ok(())
    }

    pub async fn run(&self) -> Result<(), anyhow::Error> {
        let bin = self.pipeline.clone().upcast::<gst::Bin>();
        let mut gw = janus::JanusGateway::new(bin).await?;

        // Asynchronously set the pipeline to Playing
        self.pipeline.call_async(|pipeline| {
            // If this fails, post an error on the bus so we exit
            if pipeline.set_state(gst::State::Playing).is_err() {
                gst::element_error!(
                    pipeline,
                    gst::LibraryError::Failed,
                    ("Failed to set pipeline to Playing")
                );
            }
        });

        gw.run().await?;
        Ok(())
    }
}

// Make sure to shut down the pipeline when it goes out of scope
// to release any system resources
impl Drop for AppInner {
    fn drop(&mut self) {
        let _ = self.pipeline.set_state(gst::State::Null);
    }
}

// Check if all GStreamer plugins we require are available
fn check_plugins() -> Result<(), anyhow::Error> {
    let needed = [
        "videotestsrc",
        "videoconvertscale",
        "autodetect",
        "vpx",
        "webrtc",
        "nice",
        "dtls",
        "srtp",
        "rtpmanager",
        "rtp",
    ];

    let registry = gst::Registry::get();
    let missing = needed
        .iter()
        .filter(|n| registry.find_plugin(n).is_none())
        .cloned()
        .collect::<Vec<_>>();

    if !missing.is_empty() {
        bail!("Missing plugins: {missing:?}");
    } else {
        Ok(())
    }
}

async fn async_main() -> Result<(), anyhow::Error> {
    gst::init()?;
    check_plugins()?;
    let app = App::new()?;
    app.run().await?;
    Ok(())
}

fn main() -> Result<(), anyhow::Error> {
    env_logger::init();
    let main_context = glib::MainContext::default();
    main_context.block_on(async_main())
}
