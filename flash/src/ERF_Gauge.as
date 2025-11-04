import flash.display.BitmapData;

class ERF_Gauge extends MovieClip
{
  private var gauge_mc:MovieClip;

  // ====== geometria/layout ======
  private var rOut:Number;
  private var strokePx:Number;

  private var _slotSpacingPx:Number;
  private var _slotBaseOffsetX:Number;
  private var _slotScale:Number;

  // ====== estado ======
  private var _ready:Boolean;
  private var _tried:Boolean;

  private var _slotMcs:Array;

  // ================= ctor =================
  function ERF_Gauge()
  {
    rOut        = 7;
    strokePx    = 1.5;

    _slotSpacingPx   = 40;
    _slotBaseOffsetX = -40;
    _slotScale       = 1.0;

    _ready = false;
    _tried = false;

    _slotMcs = [];
  }

  // ---------- utils ----------
  private function _sum(a:Array):Number {
    var s:Number = 0;
    for (var i:Number = 0; i < a.length; ++i) {
      var v:Number = Number(a[i]); if (!isNaN(v)) s += v;
    }
    return s;
  }

  private function _drawArc(mc:MovieClip, f:Number, t:Number, color:Number, alpha:Number):Void {
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
    for (var j:Number = 1; j <= steps; j++) {
      ang = a0 + (a1 - a0) * (j / steps);
      mc.lineTo(Math.cos(ang)*rOut, Math.sin(ang)*rOut);
    }
  }

  private function _drawFilledCircle(mc:MovieClip, r:Number, rgb:Number, alpha:Number):Void {
    mc.clear();
    mc.lineStyle(0, 0x000000, 0);
    mc.beginFill(rgb, alpha);
    var steps:Number = 64;
    for (var k:Number = 0; k <= steps; k++) {
      var a:Number = (k/steps) * Math.PI * 2;
      var x:Number = Math.cos(a) * r;
      var y:Number = Math.sin(a) * r;
      if (k == 0) mc.moveTo(x, y); else mc.lineTo(x, y);
    }
    mc.endFill();
  }

  // ============ ciclo de vida ============
  public function onLoad():Void {
    var off:Number = rOut + 2;

    if (gauge_mc) gauge_mc.removeMovieClip();
    var d:Number = this.getNextHighestDepth();
    gauge_mc = this.createEmptyMovieClip("gauge_mc", d);
    gauge_mc._x = off;
    gauge_mc._y = off;

    _ready = true;
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

  // ====== helpers de SLOT ======
  private function _ensureSlot(i:Number):MovieClip {
    if (_slotMcs[i]) return _slotMcs[i];

    var slotDepth:Number = 200 + i;
    var slot:MovieClip = gauge_mc.createEmptyMovieClip("slot_"+i, slotDepth);
    slot._xscale = slot._yscale = _slotScale * 100;

    // subcamadas
    slot.halo_mc     = slot.createEmptyMovieClip("halo_mc",     0);
    slot.ring_bg_mc  = slot.createEmptyMovieClip("ring_bg_mc", 10);
    slot.ring_fg_mc  = slot.createEmptyMovieClip("ring_fg_mc", 20);
    slot.combo_mc    = slot.createEmptyMovieClip("combo_mc",   30);

    var haloMargin:Number = 2;
    var haloR:Number = rOut + (strokePx * 0.5) + haloMargin;
    _drawFilledCircle(slot.halo_mc, haloR, 0x000000, 100);

    _slotMcs[i] = slot;
    return slot;
  }

  private function _slotClear(slot:MovieClip):Void {
    if (!slot) return;
    if (slot.ring_bg_mc) slot.ring_bg_mc.clear();
    if (slot.ring_fg_mc) slot.ring_fg_mc.clear();
    if (slot.combo_mc)   slot.combo_mc.clear();
  }

  private function _slotDrawCombo(slot:MovieClip, frac:Number, rgb:Number):Void {
    var f:Number = (isNaN(frac)) ? 0 : Math.max(0, Math.min(1, frac));
    slot.ring_bg_mc.clear();
    _drawArc(slot.ring_bg_mc, 0, 1, 0x000000, 30);

    if (f <= 0) { slot._visible = false; return; }

    var col:Number = (!isNaN(rgb) && rgb != undefined) ? Number(rgb) : 0xFFFFFF;
    slot.combo_mc.clear();
    _drawArc(slot.combo_mc, 0, f, col, 100);

    slot._visible = true;
  }

  private function _slotDrawAccum(slot:MovieClip, values:Array, colors:Array):Void {
    slot.ring_bg_mc.clear();
    slot.ring_fg_mc.clear();

    if (!values || !colors || values.length != colors.length) { slot._visible = false; return; }

    var totalRaw:Number = _sum(values);
    if (totalRaw <= 0) { slot._visible = false; return; }

    var totalShown:Number = Math.min(100, totalRaw);
    var scale:Number      = totalShown / totalRaw;

    _drawArc(slot.ring_bg_mc, 0, 1, 0x000000, 30);

    var cur:Number = 0;
    for (var i2:Number = 0; i2 < values.length; ++i2) {
      var share:Number = Number(values[i2]);
      if (isNaN(share) || share <= 0) continue;

      share *= scale;
      var seg:Number = (share / 100.0);

      var col2:Number = Number(colors[i2]);
      if (isNaN(col2)) col2 = 0xFFFFFF;

      _drawArc(slot.ring_fg_mc, cur, cur + seg, col2, 100);
      cur += seg;
      if (cur >= 1) break;
    }

    slot._visible = true;
  }

  // ============ API ============
  public function setAll(comboRemain01:Array, comboTints:Array,
                         accumValues:Array, accumColors:Array, isSingle:Boolean):Boolean
  {
    if (!_ready) _tryInit();

    var n:Number = (comboRemain01 != null) ? comboRemain01.length : 0;
    if (comboTints == null) comboTints = [];

    for (var i:Number = 0; i < n; ++i) {
      var slot:MovieClip = _ensureSlot(i);
      slot._x = _slotBaseOffsetX + (i * _slotSpacingPx);
      slot._y = 0;

      _slotClear(slot);
      var remain:Number = Number(comboRemain01[i]);
      var tint:Number = Number(comboTints[i]);

      _slotDrawCombo(slot, remain, tint);
      slot._visible = true;
    }
    
    var nextIndex:Number = n;
    var anyAccumDrawn:Boolean = false;

    var hasAccum:Boolean = false;
    if (accumValues != null) {
      var sum:Number = 0;
      for (var si:Number = 0; si < accumValues.length; ++si) {
        var vv:Number = Number(accumValues[si]);
        if (!isNaN(vv)) sum += vv;
      }
      hasAccum = (sum > 0);
    }

    if (hasAccum) {
      if (isSingle) {
        // ===== MODO SINGLE =====
        var m:Number = accumValues.length;
        var drawn:Number = 0;
        for (var k:Number = 0; k < m; ++k) {
          var v:Number = Number(accumValues[k]);
          if (isNaN(v) || v <= 0) continue;

          var frac:Number = v / 100.0;
          if (frac < 0) frac = 0;
          if (frac > 1) frac = 1;

          var col:Number = (accumColors && !isNaN(Number(accumColors[k])))
                          ? Number(accumColors[k]) : 0xFFFFFF;

          var s:MovieClip = _ensureSlot(nextIndex + drawn);
          s._x = _slotBaseOffsetX + ((nextIndex + drawn) * _slotSpacingPx);
          s._y = 0;

          _slotClear(s);
          _slotDrawCombo(s, frac, col);
          s._visible = true;

          drawn++;
        }
        if (drawn > 0) {
          anyAccumDrawn = true;
          nextIndex += drawn;
        }
      } else {
        // ===== MIXED =====
        var aSlot:MovieClip = _ensureSlot(nextIndex);
        aSlot._x = _slotBaseOffsetX + (nextIndex * _slotSpacingPx);
        aSlot._y = 0;

        _slotClear(aSlot);
        _slotDrawAccum(aSlot, accumValues, accumColors);
        aSlot._visible = true;

        anyAccumDrawn = true;
        nextIndex += 1;
      }
    }

    for (var j:Number = nextIndex; j < _slotMcs.length; ++j) {
      if (_slotMcs[j]) {
        _slotClear(_slotMcs[j]);
        _slotMcs[j]._visible = false;
      }
    }

    var anyVisible:Boolean = (n > 0) || anyAccumDrawn;
    this._visible = anyVisible;
    return anyVisible;
  }
}