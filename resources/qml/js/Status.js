.pragma library

// Mirror of yt::ServiceRequest::Status (src/requests/servicerequest.h). The models and
// detail objects expose `status` as a plain int, so QML compares against these names
// instead of magic numbers.
var Null     = 0;
var Loading  = 1;
var Canceled = 2;
var Ready    = 3;
var Failed   = 4;
