import QtQuick 1.1
import "../../js/UIConstants.js" as UI

// Horizontal snap-one-page pager with launch settling (shared by MainPage's category
// feeds and SearchPage's result types). In QtQuick 1.1 the window/ListView width goes
// 0 -> interim -> final at startup; StrictlyEnforceRange then recomputes currentIndex
// from stale contentX (and vice versa), desyncing the page from its indicator in either
// direction. Until the churn stops (readyTimer, restarted by every width change) the
// pager is pinned to `wantedIndex` as a fixed point: index flaps snap back, contentX is
// re-glued on each width change. `ready` then unlocks animated moves.
//
// The parent sets model/delegate/anchors, drives the target with select()/resetTo(),
// reads `ready` (e.g. to gate a chip strip's auto-scroll), and mirrors the landed page
// into its own selected-id state in `onSettled`.
ListView {
    id: pager
    orientation: ListView.Horizontal
    snapMode: ListView.SnapOneItem
    highlightRangeMode: ListView.StrictlyEnforceRange
    preferredHighlightBegin: 0
    preferredHighlightEnd: 0
    highlightMoveDuration: ready ? UI.ANIM_DEFAULT : 0
    boundsBehavior: Flickable.StopAtBounds
    cacheBuffer: 0
    clip: true

    property int wantedIndex: 0
    property bool ready: false
    // Fired when the pager lands on a page (swipe or programmatic move).
    signal settled(int index)

    Timer { id: readyTimer; interval: UI.ANIM_SLOW; onTriggered: pager.ready = true }

    // Content exactly on the wanted page for the CURRENT width, so range enforcement has
    // nothing to "correct" at any interim size.
    function pin() { if (width > 0) contentX = wantedIndex * width; }

    // Snap to index i and (re)enter the settling window — initial load, or a fresh
    // dataset (e.g. a new search). No animation.
    function resetTo(i) {
        wantedIndex = i;
        ready = false;
        currentIndex = i;
        pin();
        readyTimer.restart();
    }

    // Move to index i as a normal selection (chip tap). Animates once ready; still syncs
    // when already on i.
    function select(i) {
        wantedIndex = i;
        if (currentIndex === i) settled(i);
        else currentIndex = i;
    }

    onCurrentIndexChanged: {
        // Launch guard: enforcement-driven flaps snap back to the wanted page.
        if (!ready && currentIndex !== wantedIndex) {
            currentIndex = wantedIndex;
            return;
        }
        settled(currentIndex);
    }
    onWidthChanged: if (!ready) { pin(); readyTimer.restart(); }
}
