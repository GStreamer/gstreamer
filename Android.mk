# This file is the top android makefile for all sub-modules.

LOCAL_PATH := $(call my-dir)

GSTREAMER_TOP := $(LOCAL_PATH)

include $(CLEAR_VARS)

include $(GSTREAMER_TOP)/android/gst.mk
include $(GSTREAMER_TOP)/android/base.mk
include $(GSTREAMER_TOP)/android/controller.mk
include $(GSTREAMER_TOP)/android/dataprotocol.mk
include $(GSTREAMER_TOP)/android/net.mk
include $(GSTREAMER_TOP)/android/elements.mk
include $(GSTREAMER_TOP)/android/indexers.mk
include $(GSTREAMER_TOP)/android/tools.mk

