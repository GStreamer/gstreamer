# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4
#
# gst-python - Python bindings for GStreamer
# Copyright (C) 2024 Collabora Ltd
#  Author: Olivier Crête <olivier.crete@collabora.com>
# Copyright (C) 2024 Intel Corporation
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA

import overrides_hack
overrides_hack

from common import TestCase

import gi
gi.require_version("GLib", "2.0")
gi.require_version("Gst", "1.0")
gi.require_version("GstAnalytics", "1.0")
gi.require_version("GstVideo", "1.0")
from gi.repository import GLib
from gi.repository import Gst
from gi.repository import GstAnalytics
from gi.repository import GstVideo
Gst.init(None)


class TestAnalyticsODMtd(TestCase):
    def test(self):
        buf = Gst.Buffer()
        self.assertIsNotNone(buf)

        meta = GstAnalytics.buffer_add_analytics_relation_meta(buf)
        self.assertIsNotNone(meta)

        m2 = GstAnalytics.buffer_get_analytics_relation_meta(buf)
        self.assertEqual(meta, m2)

        qk = GLib.quark_from_string("testQuark")

        (ret, mtd) = meta.add_od_mtd(qk, 10, 20, 30, 40, 0.3)
        self.assertTrue(ret)
        self.assertIsNotNone(mtd)

        (ret, mtd) = meta.get_od_mtd(0)
        self.assertTrue(ret)
        self.assertIsNotNone(mtd)

        # Ensure there is no mtd 1, only 0
        (ret, _) = meta.get_mtd(1, GstAnalytics.MTD_TYPE_ANY)
        self.assertFalse(ret)

        # The is only one od mtd
        (ret, _) = meta.get_od_mtd(1)
        self.assertFalse(ret)

        # There is no Class mtd
        (ret, _) = meta.get_cls_mtd(0)
        self.assertFalse(ret)

        # meta and m2 should return the same tuple
        self.assertEqual(meta.get_od_mtd(0)[1].get_location(),
                         m2.get_od_mtd(0)[1].get_location())

        self.assertEqual(mtd.get_obj_type(), qk)

        location = meta.get_od_mtd(0)[1].get_location()
        self.assertEqual(location[1], 10)
        self.assertEqual(location[2], 20)
        self.assertEqual(location[3], 30)
        self.assertEqual(location[4], 40)
        self.assertAlmostEqual(location[5], 0.3, 3)

        location = meta.get_od_mtd(0)[1].get_oriented_location()
        self.assertEqual(location[1], 10)
        self.assertEqual(location[2], 20)
        self.assertEqual(location[3], 30)
        self.assertEqual(location[4], 40)
        self.assertEqual(location[5], 0)
        self.assertAlmostEqual(location[6], 0.3, 3)

        (ret, mtd) = meta.add_oriented_od_mtd(qk, 600, 400, 200, 100, 0.785, 0.3)
        self.assertTrue(ret)
        self.assertIsNotNone(mtd)

        (ret, mtd) = meta.get_od_mtd(1)
        self.assertTrue(ret)
        self.assertIsNotNone(mtd)

        location = mtd.get_oriented_location()
        self.assertEqual(location[1], 600)
        self.assertEqual(location[2], 400)
        self.assertEqual(location[3], 200)
        self.assertEqual(location[4], 100)
        self.assertAlmostEqual(location[5], 0.785, 3)
        self.assertAlmostEqual(location[6], 0.3, 3)

        location = mtd.get_location()
        self.assertEqual(location[1], 594)
        self.assertEqual(location[2], 344)
        self.assertEqual(location[3], 212)
        self.assertEqual(location[4], 212)
        self.assertAlmostEqual(location[5], 0.3, 3)


class TestAnalyticsClsMtd(TestCase):
    def test(self):
        buf = Gst.Buffer()
        self.assertIsNotNone(buf)

        meta = GstAnalytics.buffer_add_analytics_relation_meta(buf)
        self.assertIsNotNone(meta)

        qks = (GLib.quark_from_string("q1"),
               GLib.quark_from_string("q2"),
               GLib.quark_from_string("q3"))

        (ret, mtd) = meta.add_cls_mtd([0.1, 0.2, 0.3], qks)
        self.assertTrue(ret)
        self.assertIsNotNone(mtd)

        cnt = mtd.get_length()
        self.assertEqual(cnt, 3)

        for i in range(cnt):
            self.assertEqual(mtd.get_index_by_quark(qks[i]), i)
            self.assertAlmostEqual(mtd.get_level(i), (i + 1) / 10, 7)
            self.assertEqual(mtd.get_quark(i), qks[i])


class TestAnalyticsTrackingMtd(TestCase):
    def test(self):
        buf = Gst.Buffer()
        self.assertIsNotNone(buf)

        meta = GstAnalytics.buffer_add_analytics_relation_meta(buf)
        self.assertIsNotNone(meta)

        (ret, mtd) = meta.add_tracking_mtd(1, 10)
        self.assertTrue(ret)
        rets = mtd.get_info()
        self.assertFalse(rets.tracking_lost)
        self.assertEqual(rets.tracking_first_seen, 10)
        self.assertEqual(rets.tracking_last_seen, 10)

        mtd.update_last_seen(20)

        rets = mtd.get_info()
        self.assertEqual(rets.tracking_first_seen, 10)
        self.assertEqual(rets.tracking_last_seen, 20)

        mtd.set_lost()
        rets = mtd.get_info()
        self.assertTrue(rets.tracking_lost)


class TestAnalyticsSegmentationMtd(TestCase):
    def test(self):
        buf = Gst.Buffer()
        self.assertIsNotNone(buf)

        meta = GstAnalytics.buffer_add_analytics_relation_meta(buf)
        self.assertIsNotNone(meta)

        mask_buf = Gst.Buffer.new_allocate(None, 100, None)
        GstVideo.buffer_add_video_meta(mask_buf,
                                       GstVideo.VideoFrameFlags.NONE,
                                       GstVideo.VideoFormat.GRAY8, 10, 10)

        (ret, mtd) = meta.add_segmentation_mtd(mask_buf,
                                               GstAnalytics.SegmentationType.SEMANTIC,
                                               [7, 4, 2], 0, 0, 7, 13)
        self.assertTrue(ret)

        self.assertEqual((mask_buf, 0, 0, 7, 13), mtd.get_mask())
        self.assertEqual(mtd.get_region_count(), 3)
        self.assertEqual(mtd.get_region_id(0), 7)
        self.assertEqual(mtd.get_region_id(1), 4)
        self.assertEqual(mtd.get_region_id(2), 2)

        self.assertEqual(mtd.get_region_index(1), (False, 0))
        self.assertEqual(mtd.get_region_index(7), (True, 0))
        self.assertEqual(mtd.get_region_index(4), (True, 1))
        self.assertEqual(mtd.get_region_index(2), (True, 2))


class TestAnalyticsTensorMeta(TestCase):
    def test(self):
        buf = Gst.Buffer()
        self.assertIsNotNone(buf)

        tmeta = GstAnalytics.buffer_add_tensor_meta(buf)
        self.assertIsNotNone(tmeta)

        data = Gst.Buffer.new_allocate(None, 2 * 3 * 4)
        self.assertIsNotNone(data)

        tensor = GstAnalytics.Tensor.new_simple(0, GstAnalytics.TensorDataType.UINT8,
                                                data,
                                                GstAnalytics.TensorDimOrder.ROW_MAJOR,
                                                [1, 2, 3, 4])
        self.assertIsNotNone(tensor)
        self.assertEqual(tensor.id, 0)
        self.assertEqual(tensor.num_dims, 4)
        dims = tensor.get_dims()
        self.assertEqual(len(dims), 4)
        self.assertEqual(dims[0], 1)
        self.assertEqual(dims[1], 2)
        self.assertEqual(dims[2], 3)
        self.assertEqual(dims[3], 4)
        self.assertEqual(tensor.data, data)
        self.assertEqual(tensor.data_type, GstAnalytics.TensorDataType.UINT8)
        self.assertEqual(tensor.dims_order, GstAnalytics.TensorDimOrder.ROW_MAJOR)

        data2 = Gst.Buffer.new_allocate(None, 2 * 3 * 4 * 5)
        tensor2 = GstAnalytics.Tensor.new_simple(0, GstAnalytics.TensorDataType.UINT16,
                                                 data2,
                                                 GstAnalytics.TensorDimOrder.ROW_MAJOR,
                                                 [1, 3, 4, 5])
        tmeta.set([tensor, tensor2])

        tmeta2 = GstAnalytics.buffer_get_tensor_meta(buf)
        self.assertEqual(tmeta2.num_tensors, 2)
        self.assertEqual(tmeta2.get(0).data, data)
        self.assertEqual(tmeta2.get(1).data, data2)

        data3 = Gst.Buffer.new_allocate(None, 30)
        tensor3 = GstAnalytics.Tensor.new_simple(0,
                                                 GstAnalytics.TensorDataType.UINT16,
                                                 data3,
                                                 GstAnalytics.TensorDimOrder.ROW_MAJOR,
                                                 [0, 2, 5])
        self.assertIsNotNone(tensor3)


class TestAnalyticsRelationMetaIterator(TestCase):
    def test(self):
        buf = Gst.Buffer()
        self.assertIsNotNone(buf)

        rmeta = GstAnalytics.buffer_add_analytics_relation_meta(buf)
        self.assertIsNotNone(rmeta)

        mask_buf = Gst.Buffer.new_allocate(None, 100, None)
        GstVideo.buffer_add_video_meta(mask_buf,
                                       GstVideo.VideoFrameFlags.NONE,
                                       GstVideo.VideoFormat.GRAY8, 10, 10)

        (_, od_mtd) = rmeta.add_od_mtd(GLib.quark_from_string("od"), 1, 1, 2, 2, 0.1)
        (_, od_mtd1) = rmeta.add_od_mtd(GLib.quark_from_string("od"), 1, 1, 2, 2, 0.1)
        (_, od_mtd2) = rmeta.add_od_mtd(GLib.quark_from_string("od"), 1, 1, 2, 2, 0.1)
        (_, cls_mtd) = rmeta.add_one_cls_mtd(0.1, GLib.quark_from_string("cls"))
        (_, cls_mtd1) = rmeta.add_one_cls_mtd(0.4, GLib.quark_from_string("cls"))
        (_, trk_mtd) = rmeta.add_tracking_mtd(1, 10)
        (_, trk_mtd1) = rmeta.add_tracking_mtd(1, 11)
        (_, seg_mtd) = rmeta.add_segmentation_mtd(mask_buf,
                                                  GstAnalytics.SegmentationType.SEMANTIC,
                                                  [7, 4, 2], 0, 0, 7, 13)

        mtds = [
            (od_mtd, GstAnalytics.ODMtd.get_mtd_type()),
            (od_mtd1, GstAnalytics.ODMtd.get_mtd_type()),
            (od_mtd2, GstAnalytics.ODMtd.get_mtd_type()),
            (cls_mtd, GstAnalytics.ClsMtd.get_mtd_type()),
            (cls_mtd1, GstAnalytics.ClsMtd.get_mtd_type()),
            (trk_mtd, GstAnalytics.TrackingMtd.get_mtd_type()),
            (trk_mtd1, GstAnalytics.TrackingMtd.get_mtd_type()),
            (seg_mtd, GstAnalytics.SegmentationMtd.get_mtd_type())
        ]

        od_index_mtds = [0, 1, 2]
        cls_index_mtds = [3, 4]
        trk_index_mtds = [5, 6]
        seg_index_mtds = [7]

        mtds_from_iter = list(rmeta)

        self.assertEqual(len(mtds), len(mtds_from_iter))

        # Iterating on type GstAnalytics.ODMtd
        for j, i in zip(od_index_mtds, rmeta.iter_on_type(GstAnalytics.ODMtd)):
            assert mtds[j][0] == i
            assert mtds[j][0].id == i.id
            assert mtds[j][0].meta == i.meta
            assert mtds[j][1] == i.get_mtd_type()
            # call a method to ensure it's a ODMtd
            loc = i.get_location()

        # Iterating on type GstAnalytics.ClsMtd
        for j, i in zip(cls_index_mtds, rmeta.iter_on_type(GstAnalytics.ClsMtd)):
            assert mtds[j][0] == i
            assert mtds[j][0].id == i.id
            assert mtds[j][0].meta == i.meta
            assert mtds[j][1] == i.get_mtd_type()
            # call a method to ensure it's a ClsMtd
            level = i.get_level(0)

        # Iterating on type GstAnalytics.TrackingMtd
        for j, i in zip(trk_index_mtds, rmeta.iter_on_type(GstAnalytics.TrackingMtd)):
            assert mtds[j][0] == i
            assert mtds[j][0].id == i.id
            assert mtds[j][0].meta == i.meta
            assert mtds[j][1] == i.get_mtd_type()
            # call a method to ensure it's a TrackingMtd
            info = i.get_info()

        # Iterating on type GstAnalytics.SegmentationMtd
        for j, i in zip(seg_index_mtds, rmeta.iter_on_type(GstAnalytics.SegmentationMtd)):
            assert mtds[j][0] == i
            assert mtds[j][0].id == i.id
            assert mtds[j][0].meta == i.meta
            assert mtds[j][1] == i.get_mtd_type()
            # call a method to ensure it's a SegmentationMtd
            mask = i.get_mask()

        # Iterating on all type
        for e, i in zip(mtds, rmeta):
            assert i == e[0]
            assert e[0].id == i.id
            assert e[0].meta == i.meta
            assert e[1] == i.get_mtd_type()

        # Validate that the object is really a ODMtd
        location = mtds_from_iter[0].get_location()
        self.assertEqual(location[1], 1)
        self.assertEqual(location[2], 1)
        self.assertEqual(location[3], 2)
        self.assertEqual(location[4], 2)
        self.assertAlmostEqual(location[5], 0.1, 3)

        # Test iteration over direct relation
        rmeta.set_relation(GstAnalytics.RelTypes.RELATE_TO, od_mtd.id, od_mtd1.id)
        rmeta.set_relation(GstAnalytics.RelTypes.IS_PART_OF, od_mtd.id, trk_mtd.id)
        rmeta.set_relation(GstAnalytics.RelTypes.RELATE_TO, od_mtd.id, od_mtd2.id)
        rmeta.set_relation(GstAnalytics.RelTypes.RELATE_TO, od_mtd.id, cls_mtd.id)
        expected_mtd_ids = [od_mtd1.id, od_mtd2.id, cls_mtd.id]
        expected_mtd_type = [GstAnalytics.ODMtd, GstAnalytics.ODMtd, GstAnalytics.ClsMtd]
        count = 0
        # Iterate over all type
        for mtd in od_mtd.iter_direct_related(GstAnalytics.RelTypes.RELATE_TO):
            assert mtd.id == expected_mtd_ids[count]
            assert type(mtd) is expected_mtd_type[count]
            if (type(mtd) is GstAnalytics.ODMtd):
                assert mtd.get_obj_type() == GLib.quark_from_string("od")
            elif (type(mtd) is GstAnalytics.ClsMtd):
                assert mtd.get_quark(0) == GLib.quark_from_string("cls")
            count = count + 1

        assert (count == len(expected_mtd_ids))

        # Iterate over only with type GstAnalytics.ODMtd
        count = 0
        for mtd in od_mtd.iter_direct_related(GstAnalytics.RelTypes.RELATE_TO, GstAnalytics.ODMtd):
            assert mtd.id == expected_mtd_ids[count]
            assert type(mtd) is GstAnalytics.ODMtd
            count = count + 1

        assert (count == 2)

        # Create a relation path as od_mtd -> cls_mtd -> trk_mtd -> seg_mtd
        rmeta.set_relation(GstAnalytics.RelTypes.NONE, od_mtd.id, trk_mtd.id)  # clear relation
        rmeta.set_relation(GstAnalytics.RelTypes.RELATE_TO, cls_mtd.id, trk_mtd.id)
        rmeta.set_relation(GstAnalytics.RelTypes.RELATE_TO, trk_mtd.id, seg_mtd.id)
        count = 0
        expected_rel_ids = [od_mtd.id, cls_mtd.id, trk_mtd.id, seg_mtd.id]
        for i in od_mtd.relation_path(seg_mtd, max_span=4):
            assert i == expected_rel_ids[count]
            count += 1
        assert (count == 4)


class TestModelInfo(TestCase):
    def test_modelinfo_load_not_found(self):
        """Test loading a modelinfo file that doesn't exist"""
        modelinfo = GstAnalytics.ModelInfo.load("/nonexistent/model.onnx")
        # Should return None if file not found
        self.assertIsNone(modelinfo)

    def test_modelinfo_with_temporary_file(self):
        """Test modelinfo API with a temporary modelinfo file"""
        import tempfile
        import os

        # Create a temporary modelinfo file
        modelinfo_content = """
[modelinfo]
version=1.0
group-id=test-model-v1

[input_tensor]
dims=1,224,224,3
dir=input
type=uint8
ranges=0.0,255.0

[output_tensor]
dims=1,1000
dir=output
type=float32
id=output_logits
"""

        # Create temporary file
        with tempfile.NamedTemporaryFile(mode='w', suffix='.modelinfo',
                                         delete=False) as f:
            f.write(modelinfo_content)
            temp_modelinfo = f.name

        try:
            # Remove .modelinfo extension to get model filename
            model_filename = temp_modelinfo[:-10]  # Remove '.modelinfo'

            # Load the modelinfo using ModelInfo.load()
            modelinfo = GstAnalytics.ModelInfo.load(model_filename)
            self.assertIsNotNone(modelinfo)

            # Verify it's a ModelInfo object
            self.assertIsInstance(modelinfo, GstAnalytics.ModelInfo)

            # Test get_version
            version = modelinfo.get_version()
            self.assertEqual(version, "1.0")

            # Test get_group_id
            group_id = modelinfo.get_group_id()
            self.assertEqual(group_id, "test-model-v1")

            # Test get_group_id as quark
            group_id_quark = modelinfo.get_quark_group_id()
            self.assertEqual(group_id_quark, GLib.quark_from_string("test-model-v1"))

            # Test find_tensor_name by name
            tensor_name = modelinfo.find_tensor_name(
                GstAnalytics.ModelInfoTensorDirection.INPUT,
                0,  # index
                "input_tensor",  # in_tensor_name hint
                GstAnalytics.TensorDataType.UINT8,
                [1, 224, 224, 3]  # dims
            )
            self.assertEqual(tensor_name, "input_tensor")

            # Test get_id
            output_id = modelinfo.get_id("output_tensor")
            self.assertEqual(output_id, "output_logits")

            # Test get_id as quark
            output_id_quark = modelinfo.get_quark_id("output_tensor")
            self.assertEqual(output_id_quark, GLib.quark_from_string("output_logits"))

            # Test get_input_scales_offsets
            # Case 1: uint8 input [0, 255] to target range [0, 255] (passthrough)
            # GObject Introspection returns (success, scales, offsets)
            input_mins = [0.0]  # uint8 minimum
            input_maxs = [255.0]  # uint8 maximum
            result = modelinfo.get_input_scales_offsets("input_tensor",
              input_mins, input_maxs)
            self.assertTrue(result[0])  # success
            scales = result[1]
            offsets = result[2]
            self.assertEqual(len(scales), 1)  # scales should have 1 element
            self.assertEqual(len(offsets), 1)  # offsets should have 1 element
            self.assertAlmostEqual(scales[0], 1.0, 6)  # (255-0)/(255-0) = 1.0
            self.assertAlmostEqual(offsets[0], 0.0, 6)  # 0 - 0*1.0 = 0.0

            # Test get_dims_order (should default to row-major)
            dims_order = modelinfo.get_dims_order("input_tensor")
            self.assertEqual(dims_order, GstAnalytics.TensorDimOrder.ROW_MAJOR)

            # Test get_target_ranges (returns arrays of min/max from ranges)
            result = modelinfo.get_target_ranges("input_tensor")
            # ranges field contains "0.0,255.0" so this should succeed
            self.assertTrue(result[0])  # success
            mins = result[1]
            maxs = result[2]
            self.assertEqual(len(mins), 1)  # should have 1 range
            self.assertEqual(len(maxs), 1)  # should have 1 range
            self.assertAlmostEqual(mins[0], 0.0, 6)
            self.assertAlmostEqual(maxs[0], 255.0, 6)

            # Free the modelinfo
            modelinfo.free()

        finally:
            # Clean up temporary file
            if os.path.exists(temp_modelinfo):
                os.unlink(temp_modelinfo)
            if os.path.exists(model_filename):
                os.unlink(model_filename)

    def test_modelinfo_version_major_minor(self):
        """Test modelinfo version string parsing for major and minor versions"""
        import tempfile
        import os

        # Test case: Version 1.0 (current format version)
        modelinfo_content_1_0 = """
[modelinfo]
version=1.0
group-id=test-model-v1

[input_tensor]
dims=1,224,224,3
dir=input
type=uint8

[output_tensor]
dims=1,1000
dir=output
type=float32
id=output_logits
"""

        with tempfile.NamedTemporaryFile(mode='w', suffix='.modelinfo',
                                         delete=False) as f:
            f.write(modelinfo_content_1_0)
            temp_modelinfo = f.name

        try:
            model_filename = temp_modelinfo[:-10]  # Remove '.modelinfo'
            modelinfo = GstAnalytics.ModelInfo.load(model_filename)
            self.assertIsNotNone(modelinfo)

            # Verify version string
            version = modelinfo.get_version()
            self.assertEqual(version, "1.0")

            # Parse version string to verify major and minor components
            version_parts = version.split('.')
            self.assertEqual(len(version_parts), 2)
            major_version = int(version_parts[0])
            minor_version = int(version_parts[1])
            self.assertEqual(major_version, 1)
            self.assertEqual(minor_version, 0)

            modelinfo.free()
        finally:
            if os.path.exists(temp_modelinfo):
                os.unlink(temp_modelinfo)
            if os.path.exists(model_filename):
                os.unlink(model_filename)

    def test_modelinfo_version_major_upgrade_rejected(self):
        """Test that modelinfo with unsupported major version is rejected"""
        import tempfile
        import os

        # Test case: Version 2.0 (unsupported major version)
        # The version check should reject this
        modelinfo_content_2_0 = """
[modelinfo]
version=2.0
group-id=test-model-v2

[input_tensor]
dims=1,224,224,3
dir=input
type=uint8

[output_tensor]
dims=1,1000
dir=output
type=float32
id=output_logits
"""

        with tempfile.NamedTemporaryFile(mode='w', suffix='.modelinfo',
                                         delete=False) as f:
            f.write(modelinfo_content_2_0)
            temp_modelinfo = f.name

        try:
            model_filename = temp_modelinfo[:-10]  # Remove '.modelinfo'
            # Load should fail because version 2.0 is not supported
            modelinfo = GstAnalytics.ModelInfo.load(model_filename)
            self.assertIsNone(modelinfo)
        finally:
            if os.path.exists(temp_modelinfo):
                os.unlink(temp_modelinfo)
            if os.path.exists(model_filename):
                os.unlink(model_filename)

    def test_modelinfo_input_ranges_transformations(self):
        """Test modelinfo get_input_scales_offsets with different input ranges"""
        import tempfile
        import os

        # Create a modelinfo with a tensor that expects normalized [0, 1] range
        modelinfo_content = """
[modelinfo]
version=1.0
group-id=test-model-normalization

[input_normalized]
dims=1,224,224,3
dir=input
type=uint8
ranges=0.0,1.0;0.0,1.0;0.0,1.0
"""

        with tempfile.NamedTemporaryFile(mode='w', suffix='.modelinfo',
                                         delete=False) as f:
            f.write(modelinfo_content)
            temp_modelinfo = f.name

        try:
            model_filename = temp_modelinfo[:-10]
            modelinfo = GstAnalytics.ModelInfo.load(model_filename)
            self.assertIsNotNone(modelinfo)

            # Test 1: uint8 input [0, 255] to target [0, 1] (normalization)
            # Expected: scale = (1-0)/(255-0) ≈ 0.00392, offset = 0 - 0*scale = 0.0
            input_mins = [0.0, 0.0, 0.0]
            input_maxs = [255.0, 255.0, 255.0]
            result = modelinfo.get_input_scales_offsets("input_normalized",
                input_mins, input_maxs)
            self.assertTrue(result[0])
            scales = result[1]
            offsets = result[2]
            self.assertEqual(len(scales), 3)
            for i in range(3):
                self.assertAlmostEqual(scales[i], 1.0 / 255.0, 6)
                self.assertAlmostEqual(offsets[i], 0.0, 6)

            modelinfo.free()
        finally:
            if os.path.exists(temp_modelinfo):
                os.unlink(temp_modelinfo)
            if os.path.exists(model_filename):
                os.unlink(model_filename)

        # Create a modelinfo with a tensor that expects [-1, 1] range
        modelinfo_content_signed = """
[modelinfo]
version=1.0
group-id=test-model-signed

[input_signed]
dims=1,224,224,3
dir=input
type=int8
ranges=-1.0,1.0;-1.0,1.0;-1.0,1.0
"""

        with tempfile.NamedTemporaryFile(mode='w', suffix='.modelinfo',
                                         delete=False) as f:
            f.write(modelinfo_content_signed)
            temp_modelinfo = f.name

        try:
            model_filename = temp_modelinfo[:-10]
            modelinfo = GstAnalytics.ModelInfo.load(model_filename)
            self.assertIsNotNone(modelinfo)

            # Test 2: int8 input [-128, 127] to target [-1, 1]
            # Expected: scale = (1-(-1))/(127-(-128)) = 2/255 ≈ 0.00784
            #           offset = -1 - (-128)*scale = -1 + 128*0.00784 ≈ 0.00392
            input_mins = [-128.0, -128.0, -128.0]
            input_maxs = [127.0, 127.0, 127.0]
            result = modelinfo.get_input_scales_offsets("input_signed",
              input_mins, input_maxs)
            self.assertTrue(result[0])
            scales = result[1]
            offsets = result[2]
            self.assertEqual(len(scales), 3)
            expected_scale = 2.0 / 255.0
            expected_offset = -1.0 - (-128.0) * expected_scale
            for i in range(3):
                self.assertAlmostEqual(scales[i], expected_scale, 6)
                self.assertAlmostEqual(offsets[i], expected_offset, 6)

            modelinfo.free()
        finally:
            if os.path.exists(temp_modelinfo):
                os.unlink(temp_modelinfo)
            if os.path.exists(model_filename):
                os.unlink(model_filename)

    def test_modelinfo_version_minor_upgrade_accepted(self):
        """Test that modelinfo with same major version but higher minor version is accepted"""
        import tempfile
        import os

        # Test case: Version 1.5 (same major version, higher minor version)
        # The version check should accept this since it's backward compatible
        modelinfo_content_1_5 = """[modelinfo]
version=1.5
group-id=test-model-v1-5

[input_tensor]
dims=1,224,224,3
dir=input
type=uint8

[output_tensor]
dims=1,1000
dir=output
type=float32
id=output_logits
"""

        with tempfile.NamedTemporaryFile(mode='w', suffix='.modelinfo',
                                         delete=False) as f:
            f.write(modelinfo_content_1_5)
            temp_modelinfo = f.name

        try:
            model_filename = temp_modelinfo[:-10]  # Remove '.modelinfo'
            # Load should succeed because version 1.5 is compatible with 1.0
            # (same major version)
            modelinfo = GstAnalytics.ModelInfo.load(model_filename)
            self.assertIsNotNone(modelinfo)

            # Verify version string
            version = modelinfo.get_version()
            self.assertEqual(version, "1.5")

            # Parse version string to verify major and minor components
            version_parts = version.split('.')
            self.assertEqual(len(version_parts), 2)
            major_version = int(version_parts[0])
            minor_version = int(version_parts[1])
            self.assertEqual(major_version, 1)
            self.assertEqual(minor_version, 5)

            modelinfo.free()
        finally:
            if os.path.exists(temp_modelinfo):
                os.unlink(temp_modelinfo)
            if os.path.exists(model_filename):
                os.unlink(model_filename)
