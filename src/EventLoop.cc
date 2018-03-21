#include <unistd.h>
#include <uv.h>
#include <atomic>

#include "../ui.h"
#include "nbind/nbind.h"

extern int uiEventsPending();
extern int uiLoopWakeup();
extern int waitForNodeEvents(uv_loop_t* loop, int timeout);

static std::atomic<bool> running;
static std::atomic<bool> guiBlocked;

static uv_thread_t* thread;
static uv_timer_t* redrawTimer;
/*
   This function is executed in the
   background thread and is responsible to continuosly polling
   the node backend for pending events.

   When pending node events are found, the native GUI
   event loop is wake up by calling uiLoopWakeup().
*/
static void backgroundNodeEventsPoller(void* arg) {
  while (running) {
    int timeout = uv_backend_timeout(uv_default_loop());

    /* wait for 1s by default */
    if (timeout == 0) {
      timeout = 1000;
    }
    int pendingEvents = 1;

    /* wait for pending */
    if (timeout != -1) {
      do {
        // printf("entering waitForNodeEvents with timeout %d\n", timeout);
        pendingEvents = waitForNodeEvents(uv_default_loop(), timeout);
        // printf("exit waitForNodeEvents\n");
      } while (pendingEvents == -1 && errno == EINTR);
    }

    // printf("guiBlocked && pendingEvents %s && %d\n", guiBlocked ? "blocked" :
    // "non blocked", pendingEvents );
    if (guiBlocked && pendingEvents > 0) {
      printf("wake up neo\n");
      uiLoopWakeup();
      usleep(50 * 1000);
    }
  }
}

/*
    This function run all pending native GUI event in the loop
    using libui calls.

    It first do a blocking call to uiMainStep that
    wait for pending GUI events.
    The function also exit when there are pending node
    events, because uiLoopWakeup function posts a GUI event
    from the background thread for this purpose.
 */
void redraw(uv_timer_t* handle) {
  if (!running) {
    return;
  }
  uv_timer_stop(handle);
  Nan::HandleScope scope;

  /* Blocking call that wait for a node or GUI event pending */
  printf("blocking GUI\n");
  guiBlocked = true;
  uiMainStep(true);
  guiBlocked = false;
  printf("unblocking GUI\n");

  /* dequeue and run every event pending */
  while (uiEventsPending()) {
    running = uiMainStep(false);
  }

  printf("rescheduling\n");

  // schedule another call to redraw as soon as possible
  // how to find a correct amount of time to scheduke next call?
  //.because too long and UI is not responsive, too short and node
  // become really slow
  uv_timer_start(handle, redraw, 10, 0);
}

/* This function start the event loop and exit immediately */
void stopAsync(uv_timer_t* handle) {
  // printf("stopAsync\n");

  /* if the loop is already running, this is a noop */
  if (!running) {
    return;
  }
  running = false;

  /* stop redraw handler */
  uv_timer_stop(redrawTimer);
  uv_close((uv_handle_t*)redrawTimer, NULL);
  // printf("redrawTimer\n");

  uv_timer_stop(handle);
  uv_close((uv_handle_t*)handle, NULL);
  // printf("handle\n");

  /* await for the background thread to finish */
  // printf("11\n");
  uv_thread_join(thread);
  // printf("22\n");
  /*
    delete handle;
    delete redrawTimer;
    delete thread;
  */

  /* quit libui event loop */
  uiQuit();
}

struct EventLoop {
  /* This function start the event loop and exit immediately */
  static void start() {
    /* if the loop is already running, this is a noop */
    if (running) {
      return;
    }

    running = true;

    /* init libui event loop */
    uiMainSteps();
    // // printf("uiMainSteps...\n");

    /* start the background thread that check for node evnts pending */
    thread = new uv_thread_t();
    uv_thread_create(thread, backgroundNodeEventsPoller, NULL);
    // // printf("thread...\n");

    /* start redraw timer */
    redrawTimer = new uv_timer_t();
    uv_timer_init(uv_default_loop(), redrawTimer);
    redraw(redrawTimer);

    // // printf("redrawTimer...\n");
  }

  /* This function start the event loop and exit immediately */
  static void stop() {
    // printf("stopping\n");

    uv_timer_t* closeTimer = new uv_timer_t();
    uv_timer_init(uv_default_loop(), closeTimer);
    uv_timer_start(closeTimer, stopAsync, 1, 0);
  }
};

NBIND_CLASS(EventLoop) {
  method(start);
  method(stop);
}
