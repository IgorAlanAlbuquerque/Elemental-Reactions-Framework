import flash.display.BitmapData;

class ERF_Gauge extends MovieClip
{
  // ====== camadas ======
  private var gauge_mc:MovieClip;
  private var halo_mc:MovieClip;
  private var ring_bg_mc:MovieClip;
  private var ring_fg_mc:MovieClip;
  private var combo_mc:MovieClip;
  private var _iconMC:MovieClip;

  // ====== geometria ======
  private var rOut:Number      = 7;
  private var strokePx:Number  = 1.5;
  private var iconPadPx:Number = 0.5;
  private var iconNudgeX:Number = 0;
  private var iconNudgeY:Number = 0;

  // ====== estado ======
  private var _ready:Boolean = false;
  private var _tried:Boolean = false;

  function ERF_Gauge(){}

  // ---------- util ----------
  private function _sum(a:Array):Number
  {
    var s:Number = 0;
    for (var i:Number = 0; i < a.length; ++i) {
      var v:Number = Number(a[i]); if (!isNaN(v)) s += v;
    }
    return s;
  }

  private function _drawArc(mc:MovieClip, f:Number, t:Number, color:Number, alpha:Number):Void
  {
    if (!mc) return;
    var from:Number = Math.max(0, Math.min(1, f));
    var to:Number   = Math.max(0, Math.min(1, t));
    if (to <= from) return;

    var a0:Number = -Math.PI/2 + from * 2*Math.PI;
    var a1:Number = -Math.PI/2 + to   * 2*Math.PI;

    mc.lineStyle(strokePx, color, alpha);

    var steps:Number = Math.max(6, Math.round((to - from) * 48));
    var ang:Number = a0;
    mc.moveTo(Math.cos(ang)*rOut, Math.sin(ang)*rOut);

    for (var i:Number = 1; i <= steps; i++) {
      ang = a0 + (a1 - a0) * (i / steps);
      mc.lineTo(Math.cos(ang)*rOut, Math.sin(ang)*rOut);
    }
  }

  private function _drawFilledCircle(mc:MovieClip, r:Number, rgb:Number, alpha:Number):Void
  {
    mc.clear();
    mc.lineStyle(0, 0x000000, 0);
    mc.beginFill(rgb, alpha);
    var steps:Number = 64;
    for (var i:Number = 0; i <= steps; i++) {
      var a:Number = (i/steps) * Math.PI * 2;
      var x:Number = Math.cos(a) * r;
      var y:Number = Math.sin(a) * r;
      if (i == 0) mc.moveTo(x, y); else mc.lineTo(x, y);
    }
    mc.endFill();
  }

  private function _clearAllRings():Void
  {
    if (ring_bg_mc) ring_bg_mc.clear();
    if (ring_fg_mc) ring_fg_mc.clear();
    if (combo_mc)   combo_mc.clear();
  }

  // ============ ciclo de vida ============
  public function onLoad():Void
  {
    var off:Number = rOut + 2;

    if (gauge_mc) gauge_mc.removeMovieClip();
    gauge_mc = this.createEmptyMovieClip("gauge_mc", 100);
    gauge_mc._x = off; 
    gauge_mc._y = off;

    halo_mc        = gauge_mc.createEmptyMovieClip("halo_mc",       0);
    ring_bg_mc     = gauge_mc.createEmptyMovieClip("ring_bg_mc",   10);
    ring_fg_mc     = gauge_mc.createEmptyMovieClip("ring_fg_mc",   20);
    combo_mc       = gauge_mc.createEmptyMovieClip("combo_mc",     30);

    var haloMargin:Number = 2;
    var haloR:Number = rOut + (strokePx * 0.5) + haloMargin;
    _drawFilledCircle(halo_mc, haloR, 0x000000, 100);

    _clearAllRings();

    _ready = (ring_bg_mc != undefined && ring_fg_mc != undefined && combo_mc != undefined);
    _tried = true;
  }

  private function _tryInit():Void
  {
    if (_tried) return;
    _tried = true;
    onLoad();
  }

  public function isReady():Boolean
  {
    if (!_ready) _tryInit();
    return _ready;
  }

  // ============ API ============
  public function setIcon(path:String, rgb:Number):Boolean {
    if (_iconMC) _iconMC.removeMovieClip();
    _iconMC = gauge_mc.createEmptyMovieClip("icon_mc", 40);

    if (rgb == undefined || isNaN(rgb)) rgb = 0xFFFFFF;
    var r:Number = (rgb >> 16) & 0xFF;
    var g:Number = (rgb >> 8)  & 0xFF;
    var b:Number = (rgb)       & 0xFF;

    if (path.substr(0,6) == "img://") {
      _iconMC.loadMovie(path);              
      var self:ERF_Gauge = this;
      var tries:Number = 0;
      _iconMC.onEnterFrame = function():Void {
        if (++tries > 60) { delete this.onEnterFrame; } // timeout de 1s a ~60fps

        if (this._width > 0 && this._height > 0) {     // imagem real chegou
          var side:Number = (self.rOut*2) - (self.strokePx*2) - (self.iconPadPx*2);
          this._width  = side;
          this._height = side;
          this._x = -side/2 + self.iconNudgeX;
          this._y = -side/2 + self.iconNudgeY;
          this.cacheAsBitmap = true;

          var c:Color = new Color(this);
          c.setTransform({
            ra: (r*100/255), ga: (g*100/255), ba: (b*100/255), aa:100,
            rb:0, gb:0, bb:0, ab:0
          });
          delete this.onEnterFrame;
        }
      };
      return true;
    }

    return false;
  }

  public function setComboFill(frac:Number, rgb:Number):Void
  {
    if (!_ready) _tryInit();
    var f:Number = (isNaN(frac)) ? 0 : Math.max(0, Math.min(1, frac));
    _clearAllRings();

    ring_bg_mc.clear();
    _drawArc(ring_bg_mc, 0, 1, 0x000000, 30);

    if (f <= 0) { this._visible = false; return; }

    var col:Number = (!isNaN(rgb) && rgb != undefined) ? Number(rgb) : 0xFFFFFF;
    combo_mc.clear();
    _drawArc(combo_mc, 0, f, col, 100);

    this._visible = true;
  }

  public function setAccumulators(values:Array, colors:Array):Void
  {
    if (!_ready) _tryInit();
    _clearAllRings();

    if (!values || !colors || values.length != colors.length) { this._visible = false; return; }

    var totalRaw:Number = _sum(values);
    if (totalRaw <= 0) { this._visible = false; return; }

    var totalShown:Number = Math.min(100, totalRaw);
    var scale:Number      = totalShown / totalRaw;

    ring_bg_mc.clear();
    _drawArc(ring_bg_mc, 0, 1, 0x000000, 30);

    var cur:Number = 0;
    for (var i:Number = 0; i < values.length; ++i) {
      var share:Number = Number(values[i]);
      if (isNaN(share) || share <= 0) continue;

      share *= scale;                
      var seg:Number = (share / 100.0);

      var col:Number = Number(colors[i]);
      if (isNaN(col)) col = 0xFFFFFF;

      _drawArc(ring_fg_mc, cur, cur + seg, col, 100);
      cur += seg;
      if (cur >= 1) break;
    }

    this._visible = true;
  }
}