// Anchors AUTOMOC for the Q_OBJECT in itransport.h (yt::TransportReply). The class
// is otherwise header-only (inline ctor/dtor + a pure-virtual result() + the
// finished() signal); this translation unit exists so moc_itransport.cpp is
// generated and the metaobject/signal code is compiled into meetube-core.
#include "itransport.h"
