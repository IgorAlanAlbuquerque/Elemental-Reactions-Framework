import flash.display.BitmapData;

class ERF_Gauge extends MovieClip
{
  // ====== camadas ======
  private var gauge_mc:MovieClip;       // container central
  private var halo_mc:MovieClip;        // fundo preto arredondado
  private var ring_bg_mc:MovieClip;     // trilho cinza
  private var ring_fg_mc:MovieClip;     // arcos coloridos
  private var combo_mc:MovieClip;       // arco de combo
  private var icon_container_mc:MovieClip;

  // ====== geometria ======
  private var rOut:Number      = 7;     // raio externo
  private var strokePx:Number  = 2;     // espessura do traço
  private var iconPadPx:Number = 0.5;   // espaço do ícone para dentro do anel

  // ====== estado ======
  private var _ready:Boolean = false;
  private var _tried:Boolean = false;

  // IDs de bitmap
  private var _iconBitmapIds:Array = [
    "icon_fire",
    "icon_frost",
    "icon_shock",
    "icon_fire_frost",
    "icon_fire_shock",
    "icon_frost_shock",
    "icon_fire_frost_shock"
  ];

  function ERF_Gauge(){}

  // ---------- util ----------
  private function _sum(a:Array):Number {
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

    var a0:Number = -Math.PI/2 + from * 2*Math.PI;   // todos começam no topo
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

  private function _drawFilledCircle(mc:MovieClip, r:Number, rgb:Number, alpha:Number):Void {
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

  private function _clearAllRings():Void {
    if (ring_bg_mc) ring_bg_mc.clear();
    if (ring_fg_mc) ring_fg_mc.clear();
    if (combo_mc)   combo_mc.clear();
  }

  private function _hideIcon():Void {
    if (icon_container_mc) { icon_container_mc.removeMovieClip(); icon_container_mc = null; }
  }

  // ============ ciclo de vida ============
  public function onLoad():Void
  {
    var off:Number = rOut + 2;

    if (gauge_mc) gauge_mc.removeMovieClip();
    gauge_mc = this.createEmptyMovieClip("gauge_mc", 100);
    gauge_mc._x = off;
    gauge_mc._y = off;

    halo_mc     = gauge_mc.createEmptyMovieClip("halo_mc",      0);
    ring_bg_mc  = gauge_mc.createEmptyMovieClip("ring_bg_mc",  10);
    ring_fg_mc  = gauge_mc.createEmptyMovieClip("ring_fg_mc",  20);
    combo_mc    = gauge_mc.createEmptyMovieClip("combo_mc",    30);

    var haloMargin:Number = 2;
    var haloR:Number = rOut + (strokePx * 0.5) + haloMargin;
    _drawFilledCircle(halo_mc, haloR, 0x000000, 100);

    _clearAllRings(); _hideIcon();

    _ready = (ring_bg_mc != undefined && ring_fg_mc != undefined && combo_mc != undefined);
    _tried = true;
  }

  private function _tryInit():Void {
    if (_tried) return;
    _tried = true;
    onLoad();
  }

  public function isReady():Boolean {
    if (!_ready) _tryInit();
    return _ready;
  }

  // ============ API ============
  public function setIcon(iconId:Number, tintRGB:Number, scaleMul:Number):Boolean
  {
    var idx:Number = (isNaN(iconId)) ? -1 : (iconId | 0);
    if (idx < 0 || idx >= _iconBitmapIds.length) { _hideIcon(); return false; }

    _hideIcon();

    var container:MovieClip = gauge_mc.createEmptyMovieClip("icon_container_mc", 200);
    icon_container_mc = container;

    var bmpId:String = _iconBitmapIds[idx];
    var bd:BitmapData = BitmapData.loadBitmap(bmpId);
    if (!bd) return false;

    var holder:MovieClip = container.createEmptyMovieClip("icon_holder_mc", container.getNextHighestDepth());
    holder.cacheAsBitmap = true;
    holder.attachBitmap(bd, 1, "always", true);

    var targetPx:Number = Math.max(2, (rOut - (strokePx * 0.5) - iconPadPx) * 2);
    var baseScale:Number = (targetPx / Math.max(bd.width, bd.height)) * 100;
    var mul:Number = (!isNaN(scaleMul) && scaleMul > 0) ? scaleMul : 1.0;
    holder._xscale = holder._yscale = baseScale * mul;

    holder._x = -Math.round(holder._width  / 2);
    holder._y = -Math.round(holder._height / 2);

    if (!isNaN(tintRGB)) { var c:Color = new Color(holder); c.setRGB(tintRGB); }

    return true;
  }

  public function setComboFill(frac:Number, rgb:Number):Void
  {
    if (!_ready) _tryInit();
    var f:Number = (isNaN(frac)) ? 0 : Math.max(0, Math.min(1, frac));
    _clearAllRings();

    // trilho leve (opcional)
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
    var scale:Number      = totalShown / totalRaw; // 0..1

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