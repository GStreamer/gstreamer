use dirs::cache_dir;
use glob::glob;
use std::env;
use std::fs;
use std::path::PathBuf;
use std::process::Command;

fn main() {
    // Determine the directory to use for dumping GStreamer pipelines
    let gstdot_path = env::var("GST_DEBUG_DUMP_DOT_DIR")
        .map(PathBuf::from)
        .unwrap_or_else(|_| {
            let mut path = cache_dir().expect("Failed to find cache directory");
            path.push("gstreamer-dots");
            path
        });

    let args: Vec<String> = env::args().skip(1).collect();
    let delete = args.first().map_or(true, |arg| {
        if ["--help", "-h"].contains(&arg.as_str()) {
            eprintln!("Usage: gstdump [-n | --no-delete] [command]");
            std::process::exit(1);
        }

        !["-n", "--no-delete"].contains(&arg.as_str())
    });

    // Ensure the directory exists
    fs::create_dir_all(&gstdot_path).expect("Failed to create dot directory");

    println!("Dumping GStreamer pipelines into {:?}", gstdot_path);
    let command_idx = if delete {
        // Build the glob pattern and remove existing .dot files
        let pattern = gstdot_path.join("**/*.dot").to_string_lossy().into_owned();
        println!("Removing existing .dot files matching {pattern}");
        for entry in glob(&pattern).expect("Failed to read glob pattern") {
            match entry {
                Ok(path) => {
                    if path.is_file() {
                        fs::remove_file(path).expect("Failed to remove file");
                    }
                }
                Err(e) => eprintln!("Error reading file: {}", e),
            }
        }
        0
    } else {
        1
    };

    // Set the environment variable to use the determined directory
    env::set_var("GST_DEBUG_DUMP_DOT_DIR", &gstdot_path);
    let default_pipeline_snapshot = "pipeline-snapshot(dots-viewer-ws-url=ws://127.0.0.1:3000/snapshot/,xdg-cache=true,folder-mode=numbered)";
    env::set_var(
        "GST_TRACERS",
        env::var("GST_TRACERS").map_or_else(
            |_| default_pipeline_snapshot.to_string(),
            |tracers| {
                if !tracers.contains("pipeline-snapshot") {
                    println!("pipeline-snapshot already enabled");

                    tracers
                } else {
                    format!("{tracers},{default_pipeline_snapshot}")
                }
            },
        ),
    );

    // Run the command provided in arguments
    eprintln!("Running {:?}", &args[command_idx..]);
    if args.len() >= command_idx {
        let output = Command::new(&args[command_idx])
            .args(&args[command_idx + 1..])
            .status();

        match output {
            Ok(_status) => (),
            Err(e) => eprintln!("Error: {e:?}"),
        }
    }
}
