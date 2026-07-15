var _yt_player={};
(function(g){
var signatureTimestamp=19834;
var Ap={
 rv:function(a){a.reverse()},
 sp:function(a,b){a.splice(0,b)},
 sw:function(a,b){var c=a[0];a[0]=a[b%a.length];a[b]=c}
};
Zx=function(a){a=a.split("");Ap.rv(a,1);Ap.sw(a,2);Ap.sp(a,1);Ap.sw(a,3);return a.join("")};
var Fz=function(a){var b=a.split("");b.reverse();return b.join("")};
g.foo=function(d){var c;c=d.get("n"))&&(d=Fz(c),d.set("n",d));return Zx(c)};
})(_yt_player);
