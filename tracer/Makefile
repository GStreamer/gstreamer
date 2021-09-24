TEST_DATA = \
  logs/trace.latency.log

all:

logs/trace.latency.log:
	mkdir -p logs; \
	GST_DEBUG="GST_TRACER:7" GST_TRACERS=latency GST_DEBUG_FILE=$@ \
	  gst-launch-1.0 -q audiotestsrc num-buffers=10 wave=silence ! audioconvert ! autoaudiosink

check: $(TEST_DATA)
	python3 -m unittest discover tracer "*_test.py"
