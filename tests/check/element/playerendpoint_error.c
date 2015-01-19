/*
 * (C) Copyright 2013 Kurento (http://kurento.org/)
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/check/gstcheck.h>
#include <gst/gst.h>
#include <commons/kmsuriendpointstate.h>

#include <kmstestutils.h>
#include <commons/kmselementpadtype.h>

#define VIDEO_PATH "file:///tmp/small.webm"

#define KMS_VIDEO_PREFIX "video_src_"
#define KMS_AUDIO_PREFIX "audio_src_"

#define SINK "sink"

typedef struct HandOffData
{
  gint num_buffers;
} HandOffData;

static void
bus_msg (GstBus * bus, GstMessage * msg, gpointer pipe)
{

  switch (msg->type) {
    case GST_MESSAGE_ERROR:{
      GST_ERROR ("Error: %" GST_PTR_FORMAT, msg);
      GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (pipe),
          GST_DEBUG_GRAPH_SHOW_ALL, "bus_error");
      fail ("Error received on bus");
      break;
    }
    case GST_MESSAGE_WARNING:{
      GST_WARNING ("Warning: %" GST_PTR_FORMAT, msg);
      GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (pipe),
          GST_DEBUG_GRAPH_SHOW_ALL, "warning");
      break;
    }
    default:
      break;
  }
}

static gboolean
quit_main_loop_idle (gpointer data)
{
  GMainLoop *loop = data;

  GST_DEBUG ("Test finished exiting main loop");
  g_main_loop_quit (loop);

  return FALSE;
}

static void
connect_sink_on_srcpad_added (GstElement * element, GstPad * pad,
    gpointer user_data)
{
  GstElement *sink = g_object_get_data (G_OBJECT (element), SINK);
  GstPad *sinkpad = gst_element_get_static_pad (sink, "sink");

  gst_pad_link (pad, sinkpad);
  g_object_unref (sinkpad);
  gst_element_sync_state_with_parent (sink);
}

static gboolean
kms_element_request_srcpad (GstElement * src, KmsElementPadType pad_type)
{
  gchar *padname;

  g_signal_emit_by_name (src, "request-new-srcpad", pad_type, NULL, &padname);
  if (padname == NULL) {
    return FALSE;
  }

  GST_DEBUG_OBJECT (src, "Requested pad %s", padname);
  g_free (padname);

  return TRUE;
}

static void
fakesink_hand_off (GstElement * fakesink, GstBuffer * buf, GstPad * pad,
    gpointer data)
{
  HandOffData *hod = (HandOffData *) data;

  g_atomic_int_inc (&hod->num_buffers);
}

static void
player_eos (GstElement * player, GMainLoop * loop)
{
  GST_DEBUG ("Eos received");
  g_idle_add (quit_main_loop_idle, loop);
}

GST_START_TEST (test_error)
{
  gint total_buffers;
  HandOffData *hod = g_slice_new0 (HandOffData);
  GMainLoop *loop = g_main_loop_new (NULL, TRUE);
  GstElement *pipeline = gst_pipeline_new (__FUNCTION__);
  GstBus *bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  guint bus_watch_id;
  GstElement *player = gst_element_factory_make ("playerendpoint", NULL);
  GstElement *fakesink = gst_element_factory_make ("fakesink", NULL);

  g_object_set (G_OBJECT (pipeline), "async-handling", TRUE, NULL);

  bus_watch_id = gst_bus_add_watch (bus, gst_bus_async_signal_func, NULL);
  g_signal_connect (bus, "message", G_CALLBACK (bus_msg), pipeline);
  g_object_unref (bus);

  g_object_set (G_OBJECT (fakesink), "async", FALSE, "sync", FALSE,
      "signal-handoffs", TRUE, NULL);
  g_signal_connect (G_OBJECT (fakesink), "handoff",
      G_CALLBACK (fakesink_hand_off), hod);

  g_object_set_data (G_OBJECT (player), SINK, fakesink);
  g_object_set (G_OBJECT (player), "uri", VIDEO_PATH, NULL);
  g_signal_connect (G_OBJECT (player), "eos", G_CALLBACK (player_eos), loop);
  g_signal_connect (player, "pad-added",
      G_CALLBACK (connect_sink_on_srcpad_added), NULL);

  gst_bin_add_many (GST_BIN (pipeline), player, fakesink, NULL);

  fail_unless (kms_element_request_srcpad (player, KMS_ELEMENT_PAD_TYPE_VIDEO));

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  g_object_set (G_OBJECT (player), "state", KMS_URI_ENDPOINT_STATE_START, NULL);

  GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (pipeline),
      GST_DEBUG_GRAPH_SHOW_ALL, "test_error_before_entering_loop");

  mark_point ();
  g_main_loop_run (loop);
  mark_point ();

  GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (pipeline),
      GST_DEBUG_GRAPH_SHOW_ALL, "test_error_end");

  gst_element_set_state (pipeline, GST_STATE_NULL);

  total_buffers = g_atomic_int_get (&hod->num_buffers);
  GST_INFO_OBJECT (player, "num buffers: %" G_GINT32_FORMAT, total_buffers);
  fail_if ((total_buffers > 2) && (total_buffers < 10));

  gst_object_unref (GST_OBJECT (pipeline));
  g_source_remove (bus_watch_id);
  g_main_loop_unref (loop);
  g_slice_free (HandOffData, hod);
}

GST_END_TEST
/* Define test suite */
static Suite *
playerendpoint_error_suite (void)
{
  Suite *s = suite_create ("playerendpoint_error");
  TCase *tc_chain = tcase_create ("element");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_error);

  return s;
}

GST_CHECK_MAIN (playerendpoint_error);
