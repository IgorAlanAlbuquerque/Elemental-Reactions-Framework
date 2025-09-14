// Register.as — Flash 8 / AS2
// NÃO use package; deixe no pacote padrão para facilitar o classpath.

class Register {
  static function main() : Void {
    // Se a classe estiver no pacote padrão, não precisa de import.
    // Se você colocou SMSO_Gauge em um pacote, então adicione o import aqui.
    Object.registerClass("SMSO_Gauge", SMSO_Gauge);
  }
}
