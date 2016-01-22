#include "BpmCalculator.h"
#include "debug/DebugPrint.h"


#include <exception>
#include <memory>
#include <algorithm>
#include <math.h>

using namespace std;

//Helpers
static gboolean busCallHandlerWrapper( GstBus*, GstMessage* msg, gpointer data )
{
    BpmCalculator* calc = static_cast< BpmCalculator* >( data );
    return calc->busCallHandler( msg );
}

//Class  methods
//TODO create init method separate from start
//TODO think if you really need exceptions here
void BpmCalculator::calculate( const string& filename )
{
    gst_init ( NULL, NULL );

    mPipeline.reset( gst_element_factory_make( "playbin", "pipeline" ) );

    if ( !mLoop )
    {
        DEBUG_PRINT( DL_ERROR, "Main loop couldn't be created\n" );
        throw;
    }

    if ( !mPipeline )
    {
        DEBUG_PRINT( DL_ERROR, "Playbin could not be created.\n" );
        throw;
    }
    // Set up the pipeline
    g_object_set( G_OBJECT( mPipeline.get( ) ), "uri", filename.c_str( ), NULL );

    // Add a bus message handler
    unique_ptr< GstBus, void( * )( gpointer ) > bus(
            gst_pipeline_get_bus( GST_PIPELINE ( mPipeline.get( ) ) ),
            gst_object_unref );
    mBusWatchId = gst_bus_add_watch (
            bus.get( ),
            busCallHandlerWrapper,
            static_cast< gpointer >( this ) )
    ;
    // Create a custom sink for the playbin
    GstElement* bin = gst_bin_new ( "bin" );
    GstElement* bpmDetector = gst_element_factory_make ( "bpmdetect", "detector" );
    GstElement* sink = gst_element_factory_make ( "fakesink", "sink" );
    g_object_set( G_OBJECT( sink ), "sync", FALSE, NULL );

    if ( !bin || !bpmDetector || ! sink )
    {
        DEBUG_PRINT( DL_ERROR, "Custom sink for the playbin could not be created. Player not started.\n" );
        throw;
    }

    gst_bin_add_many( GST_BIN( bin ), bpmDetector, sink, NULL );
    gst_element_link_many( bpmDetector, sink, NULL );

    unique_ptr< GstPad, void( * )( gpointer) > pad(
        gst_element_get_static_pad ( bpmDetector, "sink" ),
        gst_object_unref );
    GstPad* ghost_pad = gst_ghost_pad_new ( "sink", pad.get( ) );
    gst_pad_set_active ( ghost_pad, TRUE );
    gst_element_add_pad ( bin, ghost_pad );

    // Set custom sink to the playbin
    g_object_set( GST_OBJECT( mPipeline.get( ) ), "audio-sink", bin, NULL );

    // Start playing
    gst_element_set_state ( mPipeline.get( ), GST_STATE_PLAYING );

    DEBUG_PRINT( DL_INFO, "Starting playback\n" );

    g_main_loop_run ( mLoop.get( ) );
}

BpmCalculator::BpmCalculator( const CompletedCallback& cb )
    : mLoop( g_main_loop_new( NULL, FALSE ), g_main_loop_unref )
    , mPipeline( NULL, gst_object_unref )
    , mCallback( cb )
{}

BpmCalculator::~BpmCalculator( ) {
    gst_element_set_state ( mPipeline.get( ), GST_STATE_NULL);
    g_source_remove ( mBusWatchId );
}

gboolean BpmCalculator::busCallHandler( GstMessage* msg )
{
    switch ( GST_MESSAGE_TYPE ( msg ) ) {
    case GST_MESSAGE_EOS:
        DEBUG_PRINT( DL_INFO, "End of stream\n" );
        mCallback( calculateBpm( ) );
        g_main_loop_quit ( mLoop.get( ) );
        break;
    case GST_MESSAGE_TAG:
    {
        GstTagList *tags = NULL;
        gst_message_parse_tag ( msg, &tags );
        unique_ptr< GstTagList, void( * )( GstTagList* ) > tagsPtr( tags, gst_tag_list_unref );
        gdouble bpm = 0;
        if ( gst_tag_list_get_double(tags, "beats-per-minute", &bpm ) && bpm > 0 )
        {
            DEBUG_PRINT( DL_INFO, "Bpm %f (%d)\n", bpm, (unsigned int)round( bpm ) );
            auto ret = mBpmMap.insert( pair< unsigned int, unsigned int >( round( bpm ), 1 ) );
            if ( ret.second == false )
            {
                //element wasn't inserted - update the element
                ret.first->second += 1;
            }
        }
        break;
    }
    case GST_MESSAGE_ERROR:
        gchar  *debug;
        GError *error;
        gst_message_parse_error ( msg, &error, &debug );
        g_free ( debug );
        DEBUG_PRINT( DL_ERROR, "Error: %s\n", error->message );
        g_error_free ( error );
        g_main_loop_quit ( mLoop.get( ) );
        break;
    default:
       break;
    }
    return TRUE;
}

unsigned int BpmCalculator::calculateBpm( )
{
    auto it = std::max_element
    (
        mBpmMap.begin( ), mBpmMap.end( ),
        [] ( const decltype( mBpmMap )::value_type& p1, 
             const decltype( mBpmMap )::value_type& p2 )
            {
                return p1.second < p2.second;
            }
    );
    if ( it->second > 1 )
    {
        return it->first;
    }
    else
    {
        return 0;
    }
}












