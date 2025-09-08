class Main {
  static function main(mc:MovieClip) {
    // debug: caixa verde pra provar que o SWF carregou
    var dbg:MovieClip = mc.createEmptyMovieClip("dbg", 1);
    dbg.beginFill(0x00FF00, 40);
    dbg.moveTo(-64,-64); dbg.lineTo(64,-64); dbg.lineTo(64,64); dbg.lineTo(-64,64); dbg.lineTo(-64,-64);
    dbg.endFill();

    // instancia o símbolo exportado e conecta a sua classe
    var g:MovieClip = mc.attachMovie("SMSO_Gauge", "gauge", 10);
    g._x = 128; g._y = 128;   // centro do canvas 256x256
    // cria a lógica sobre o movie instanciado
    mc.__impl = new SMSO_Gauge(g);

    // estado inicial
    mc.__impl.setTotals(0,0,0);
    mc.__impl.setIcon(0, undefined);
    mc.__impl.setVisible(true);
  }
}
