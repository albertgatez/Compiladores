#include <iostream>
#include <string>
#include <vector>
#include <cctype>
#ifdef _WIN32
#include <windows.h>
#endif

using namespace std;

// AFD: estados
enum class Estado {
    q0,
    // Reconocimiento de mnemónicos
    qM, qMO, qMOV,
    qA, qAD, qADD,
    qS, qST, qSTO,
    qE, qEN, qEND,
    // Rama MOV R, d
    qMOV_esp, qMOV_A, qMOV_B, qMOV_reg, qMOV_pre, qMOV_coma, qMOV_num,
    // Rama STO d
    qSTO_esp, qSTO_num,
    // Rama ADD AL, BL
    qADD_esp, qADD_A, qADD_AL, qADD_pre, qADD_coma, qADD_B, qADD_BL,
    // Estado de error
    qERR
};

const char* nombreEstado(Estado e) {
    static const char* nombres[] = {
        "q0",
        "qM", "qMO", "qMOV",
        "qA", "qAD", "qADD",
        "qS", "qST", "qSTO",
        "qE", "qEN", "qEND",
        "qMOV_esp", "qMOV_A", "qMOV_B", "qMOV_reg", "qMOV_pre", "qMOV_coma", "qMOV_num",
        "qSTO_esp", "qSTO_num",
        "qADD_esp", "qADD_A", "qADD_AL", "qADD_pre", "qADD_coma", "qADD_B", "qADD_BL",
        "qERR"
    };
    return nombres[static_cast<int>(e)];
}

// ====================================================================================================
//  Estructuras de datos
// ====================================================================================================

struct Token {
    string tipo;     // MOV, ADD, STO, END, REGISTRO, COMA, NUMERO
    string lexema;   // texto original del token
};

struct ResultadoAFD {
    bool valido = false;
    string error;               // descripción del error (si no es válido)
    int posicionError = 0;      // posición (1-based) del carácter que causó el error; 0 = fin de entrada
    vector<Token> tokens;
    vector<string> traza;       // recorrido de estados
};

// Objeto instrucción (representación alterna para la máquina de Moore)
struct Instruccion {
    string operacion;
    vector<string> registros;
    bool tieneDireccion = false;
    string direccion; // dígitos decimales sin ceros a la izquierda (evita desbordes)
};

// ====================================================================================================
//  Utilidades de clasificación de caracteres
// ====================================================================================================

static bool esEspacio(char c) { return c == ' ' || c == '\t'; }
static bool esDigito(char c)  { return c >= '0' && c <= '9'; }
static bool esMinuscula(char c) { return islower(static_cast<unsigned char>(c)) != 0; }

// car quiere decir "carácter" y devuelve una cadena que describe el carácter para los mensajes de error
static string car(char c) {
    if (c == ' ')  return "espacio";
    if (c == '\t') return "tabulador";
    return string("'") + c + "'";
}

// ====================================================================================================
// AFD: función de transición, estados finales y errores
// ====================================================================================================

class AFD {
public:
    // ---- Función de transición delta(estado, carácter) -----------------------
    // Devuelve el siguiente estado. Si es qERR, 'msg' contiene la causa.
    static Estado transicion(Estado e, char c, string& msg) {
        const bool sp = esEspacio(c);
        const bool dg = esDigito(c);
        auto fallo = [&](const string& m) { msg = m; return Estado::qERR; }; // lambda para reportar errores, `[&]` captura por referencia las variables locales, o sea que modifica 'msg' y devuelve qERR
        auto mnemonicoMal = [&]() { // lambda para reportar mnemónicos mal escritos
            if (esMinuscula(c))
                return fallo("los mnemónicos y registros deben escribirse en mayúsculas (se encontró " + car(c) + ")");
            return fallo("mnemónico desconocido o mal escrito (se encontró " + car(c) + ")");
        };

        switch (e) {
        // ---------------------------------------------------------------- inicio
        case Estado::q0:
            if (c == 'M') return Estado::qM;
            if (c == 'A') return Estado::qA;
            if (c == 'S') return Estado::qS;
            if (c == 'E') return Estado::qE;
            if (sp) return fallo("hay espacios al inicio de la instrucción; no están permitidos");
            return mnemonicoMal();

        // ------------------------------------------------------------ mnemónicos
        case Estado::qM:   return c == 'O' ? Estado::qMO  : mnemonicoMal();
        case Estado::qMO:  return c == 'V' ? Estado::qMOV : mnemonicoMal();
        case Estado::qA:   return c == 'D' ? Estado::qAD  : mnemonicoMal();
        case Estado::qAD:  return c == 'D' ? Estado::qADD : mnemonicoMal();
        case Estado::qS:   return c == 'T' ? Estado::qST  : mnemonicoMal();
        case Estado::qST:  return c == 'O' ? Estado::qSTO : mnemonicoMal();
        case Estado::qE:   return c == 'N' ? Estado::qEN  : mnemonicoMal();
        case Estado::qEN:  return c == 'D' ? Estado::qEND : mnemonicoMal();

        case Estado::qMOV:
            if (sp) return Estado::qMOV_esp;
            return fallo("debe haber al menos un espacio entre MOV y sus operandos (se encontró " + car(c) + ")");
        case Estado::qADD:
            if (sp) return Estado::qADD_esp;
            return fallo("debe haber al menos un espacio entre ADD y sus operandos (se encontró " + car(c) + ")");
        case Estado::qSTO:
            if (sp) return Estado::qSTO_esp;
            return fallo("debe haber al menos un espacio entre STO y la dirección (se encontró " + car(c) + ")");
        case Estado::qEND:
            if (sp) return fallo("END no admite operandos ni espacios adicionales al final");
            return fallo("END no admite caracteres adicionales (se encontró " + car(c) + ")");

        // ------------------------------------------------------------- rama MOV
        case Estado::qMOV_esp:
            if (sp) return Estado::qMOV_esp;
            if (c == 'A') return Estado::qMOV_A;
            if (c == 'B') return Estado::qMOV_B;
            if (c == ',') return fallo("falta el registro destino antes de la coma");
            if (dg)       return fallo("falta el registro destino (AL o BL) antes de la dirección");
            return fallo("registro inválido: solo se admiten AL o BL (se encontró " + car(c) + ")");
        case Estado::qMOV_A:
        case Estado::qMOV_B:
            if (c == 'L') return Estado::qMOV_reg;
            return fallo("registro inválido: solo se admiten AL o BL (se encontró " + car(c) + " tras la primera letra)");
        case Estado::qMOV_reg:
            if (sp) return Estado::qMOV_pre;
            if (c == ',') return Estado::qMOV_coma;
            if (dg) return fallo("falta la coma entre el registro y la dirección");
            return fallo("registro inválido: solo se admiten AL o BL (se encontró " + car(c) + " después del registro)");
        case Estado::qMOV_pre:
            if (sp) return Estado::qMOV_pre;
            if (c == ',') return Estado::qMOV_coma;
            if (dg) return fallo("falta la coma entre el registro y la dirección");
            return fallo("se esperaba una coma después del registro (se encontró " + car(c) + ")");
        case Estado::qMOV_coma:
            if (sp) return Estado::qMOV_coma;
            if (dg) return Estado::qMOV_num;
            if (c == '-') return fallo("la dirección no puede ser negativa");
            if (c == ',') return fallo("coma duplicada");
            return fallo("la dirección debe ser un número decimal no negativo (se encontró " + car(c) + ")");
        case Estado::qMOV_num:
            if (dg) return Estado::qMOV_num;
            if (sp) return fallo("no se permiten espacios finales ni operandos adicionales después de la dirección");
            if (c == ',') return fallo("MOV no admite operandos adicionales");
            return fallo("carácter no numérico en la dirección (se encontró " + car(c) + ")");

        // ------------------------------------------------------------- rama STO
        case Estado::qSTO_esp:
            if (sp) return Estado::qSTO_esp;
            if (dg) return Estado::qSTO_num;
            if (c == '-') return fallo("la dirección no puede ser negativa");
            if (c == ',') return fallo("STO solo admite una dirección, no lleva coma");
            return fallo("STO requiere una dirección numérica (se encontró " + car(c) + ")");
        case Estado::qSTO_num:
            if (dg) return Estado::qSTO_num;
            if (sp) return fallo("no se permiten espacios finales ni operandos adicionales después de la dirección");
            if (c == ',') return fallo("STO no admite operandos adicionales");
            return fallo("carácter no numérico en la dirección (se encontró " + car(c) + ")");

        // ------------------------------------------------------------- rama ADD
        case Estado::qADD_esp:
            if (sp) return Estado::qADD_esp;
            if (c == 'A') return Estado::qADD_A;
            if (c == 'B') return fallo("orden incorrecto: el primer operando de ADD debe ser AL");
            if (c == ',') return fallo("falta el primer registro (AL) antes de la coma");
            if (dg)       return fallo("ADD solo admite registros (AL, BL), no direcciones");
            return fallo("ADD requiere AL como primer operando (se encontró " + car(c) + ")");
        case Estado::qADD_A:
            if (c == 'L') return Estado::qADD_AL;
            return fallo("registro inválido: se esperaba AL (se encontró " + car(c) + " tras la 'A')");
        case Estado::qADD_AL:
            if (sp) return Estado::qADD_pre;
            if (c == ',') return Estado::qADD_coma;
            return fallo("falta la coma entre AL y BL (se encontró " + car(c) + ")");
        case Estado::qADD_pre:
            if (sp) return Estado::qADD_pre;
            if (c == ',') return Estado::qADD_coma;
            return fallo("falta la coma entre AL y BL (se encontró " + car(c) + ")");
        case Estado::qADD_coma:
            if (sp) return Estado::qADD_coma;
            if (c == 'B') return Estado::qADD_B;
            if (c == 'A') return fallo("el segundo operando de ADD debe ser BL");
            if (c == ',') return fallo("coma duplicada");
            if (dg)       return fallo("ADD solo admite registros; se esperaba BL, no una dirección");
            return fallo("se esperaba BL como segundo operando (se encontró " + car(c) + ")");
        case Estado::qADD_B:
            if (c == 'L') return Estado::qADD_BL;
            return fallo("registro inválido: se esperaba BL (se encontró " + car(c) + " tras la 'B')");
        case Estado::qADD_BL:
            if (sp) return fallo("no se permiten espacios finales ni operandos adicionales después de BL");
            return fallo("ADD no admite operandos adicionales (se encontró " + car(c) + ")");

        // ------------------------------------------------------------- error
        case Estado::qERR:
        default:
            return Estado::qERR;
        }
    }

    // ---- Estados de aceptación ------------------------------------------------
    static bool esFinal(Estado e) {
        return e == Estado::qEND || e == Estado::qADD_BL ||
               e == Estado::qMOV_num || e == Estado::qSTO_num;
    }

    // ---- Error cuando la entrada termina en un estado no final -----------------
    static string mensajeFin(Estado e) {
        switch (e) {
        case Estado::q0:        return "la instrucción está vacía";
        case Estado::qM: case Estado::qMO:
        case Estado::qA: case Estado::qAD:
        case Estado::qS: case Estado::qST:
        case Estado::qE: case Estado::qEN:
                                return "mnemónico incompleto o desconocido";
        case Estado::qMOV:      return "faltan el registro y la dirección";
        case Estado::qADD:      return "faltan los registros AL y BL";
        case Estado::qSTO:      return "falta la dirección";
        case Estado::qMOV_esp:  return "falta el registro destino (AL o BL)";
        case Estado::qMOV_A:
        case Estado::qMOV_B:    return "registro incompleto: se esperaba AL o BL";
        case Estado::qADD_A:    return "registro incompleto: se esperaba AL";
        case Estado::qADD_B:    return "registro incompleto: se esperaba BL";
        case Estado::qMOV_reg:
        case Estado::qMOV_pre:  return "falta la coma y la dirección";
        case Estado::qADD_AL:
        case Estado::qADD_pre:  return "falta la coma y el segundo registro (BL)";
        case Estado::qMOV_coma: return "falta la dirección después de la coma";
        case Estado::qADD_coma: return "falta el segundo registro (BL) después de la coma";
        case Estado::qSTO_esp:  return "falta la dirección";
        default:                return "la instrucción está incompleta";
        }
    }

    // ---- Recorrido carácter por carácter --------------------------------------
    static ResultadoAFD analizar(const string& s) {
        ResultadoAFD res;
        Estado estado = Estado::q0;
        string numero;          // acumula los dígitos de la dirección
        char letraReg = 0;      // primera letra del registro en curso (A o B)

        if (s.empty()) {
            res.error = mensajeFin(Estado::q0);
            return res;
        }

        for (size_t i = 0; i < s.size(); ++i) {
            const char c = s[i];
            string msg;
            const Estado sig = transicion(estado, c, msg);
            res.traza.push_back(string(nombreEstado(estado)) + " --" + car(c) + "--> " + nombreEstado(sig));

            if (sig == Estado::qERR) {      // error: se detiene el procesamiento
                res.error = msg;
                res.posicionError = static_cast<int>(i) + 1;
                return res;
            }

            // Acciones asociadas a la entrada en cada estado (emisión de tokens)
            switch (sig) {
            case Estado::qMOV: res.tokens.push_back({"MOV", "MOV"}); break;
            case Estado::qADD: res.tokens.push_back({"ADD", "ADD"}); break;
            case Estado::qSTO: res.tokens.push_back({"STO", "STO"}); break;
            case Estado::qEND: res.tokens.push_back({"END", "END"}); break;
            case Estado::qMOV_A: case Estado::qMOV_B:
            case Estado::qADD_A: case Estado::qADD_B:
                letraReg = c; break;
            case Estado::qMOV_reg: case Estado::qADD_AL: case Estado::qADD_BL:
                res.tokens.push_back({"REGISTRO", string(1, letraReg) + "L"}); break;
            case Estado::qMOV_coma: case Estado::qADD_coma:
                if (estado != sig) {                       // solo la primera vez
                    res.tokens.push_back({"COMA", ","});
                }
                break;
            case Estado::qMOV_num: case Estado::qSTO_num:
                numero += c; break;
            default: break;
            }
            estado = sig;
        }

        // Fin de la entrada: ¿estado de aceptación?
        if (!esFinal(estado)) {
            res.error = mensajeFin(estado);
            res.posicionError = 0;
            return res;
        }
        if (estado == Estado::qMOV_num || estado == Estado::qSTO_num)
            res.tokens.push_back({"NUMERO", numero});

        res.valido = true;
        return res;
    }
};

// ====================================================================================================
// Construcción del objeto instrucción (a partir de los tokens validados)
// ====================================================================================================

Instruccion construirInstruccion(const vector<Token>& tokens) {
    Instruccion ins;
    for (const Token& t : tokens) {
        if (t.tipo == "MOV" || t.tipo == "ADD" || t.tipo == "STO" || t.tipo == "END")
            ins.operacion = t.tipo;
        else if (t.tipo == "REGISTRO")
            ins.registros.push_back(t.lexema);
        else if (t.tipo == "NUMERO") {
            size_t p = t.lexema.find_first_not_of('0');       // quita ceros a la izquierda
            ins.direccion = (p == string::npos) ? "0" : t.lexema.substr(p); // npos es el valor especial que indica que no se encontró el carácter
            ins.tieneDireccion = true;
        }
    }
    return ins;
}

// ====================================================================================================
// Máquina de Moore: la salida depende únicamente del estado
// ====================================================================================================

enum class EstadoMoore {
    PrepararDireccion,   // MAR <- d
    LeerMemoria,         // MBR <- M[MAR]
    CargarAL,            // AL <- MBR
    CargarBL,            // BL <- MBR
    Sumar,               // ACC <- AL + BL
    AlmacenarMBR,        // MBR <- ACC
    EscribirMemoria,     // M[MAR] <- MBR
    Detener,             // HALT <- 1
    Fin                  // sin salida
};

class MaquinaMoore {
public:
    // Recibe el objeto instrucción; NO vuelve a analizar el texto.
    explicit MaquinaMoore(const Instruccion& ins) : instr(ins) { // explicit es para evitar conversiones implícitas
        if      (instr.operacion == "MOV" || instr.operacion == "STO") actual = EstadoMoore::PrepararDireccion;
        else if (instr.operacion == "ADD") actual = EstadoMoore::Sumar;
        else if (instr.operacion == "END") actual = EstadoMoore::Detener;
        else                               actual = EstadoMoore::Fin;
    }

    bool terminada() const { return actual == EstadoMoore::Fin; }

    // Emite la salida del estado actual y avanza al siguiente.
    string siguientePaso() {
        string out = salida(actual);
        actual = siguiente(actual);
        return out;
    }

private:
    const Instruccion& instr;
    EstadoMoore actual;

    // Función de salida (lambda de Moore): el estado decide, el objeto aporta el operando
    string salida(EstadoMoore s) const {
        switch (s) {
        case EstadoMoore::PrepararDireccion: return "MAR ← " + instr.direccion;
        case EstadoMoore::LeerMemoria:       return "MBR ← M[MAR]";
        case EstadoMoore::CargarAL:          return "AL ← MBR";
        case EstadoMoore::CargarBL:          return "BL ← MBR";
        case EstadoMoore::Sumar:             return "ACC ← AL + BL";
        case EstadoMoore::AlmacenarMBR:      return "MBR ← ACC";
        case EstadoMoore::EscribirMemoria:   return "M[MAR] ← MBR";
        case EstadoMoore::Detener:           return "HALT ← 1";
        case EstadoMoore::Fin:               return "Ninguna";
        }
        return "";
    }

    // Función de transición de Moore
    EstadoMoore siguiente(EstadoMoore s) const {
        switch (s) {
        case EstadoMoore::PrepararDireccion:
            return instr.operacion == "MOV" ? EstadoMoore::LeerMemoria : EstadoMoore::AlmacenarMBR;
        case EstadoMoore::LeerMemoria:
            return (!instr.registros.empty() && instr.registros[0] == "AL")
                       ? EstadoMoore::CargarAL : EstadoMoore::CargarBL;
        case EstadoMoore::CargarAL:
        case EstadoMoore::CargarBL:
        case EstadoMoore::Sumar:
        case EstadoMoore::EscribirMemoria:
        case EstadoMoore::Detener:           return EstadoMoore::Fin;
        case EstadoMoore::AlmacenarMBR:      return EstadoMoore::EscribirMemoria;
        case EstadoMoore::Fin:               return EstadoMoore::Fin;
        }
        return EstadoMoore::Fin;
    }
};

// ====================================================================================================
// Impresión de resultados
// ====================================================================================================

void imprimirComponentes(const Instruccion& ins) {
    cout << "Mnemónico: " << ins.operacion << "\n";
    if (ins.operacion == "MOV") {
        cout << "Registro destino: " << ins.registros[0] << "\n";
        cout << "Dirección de memoria: " << ins.direccion << "\n";
        cout << "Direccionamiento: directo\n";
    } else if (ins.operacion == "ADD") {
        cout << "Registros operandos: " << ins.registros[0] << ", " << ins.registros[1] << "\n";
        cout << "Destino del resultado: ACC\n";
    } else if (ins.operacion == "STO") {
        cout << "Dirección de memoria: " << ins.direccion << "\n";
        cout << "Direccionamiento: directo\n";
    } else {
        cout << "Sin operandos\n";
    }
}

void imprimirObjeto(const Instruccion& ins) {
    cout << "{\n";
    cout << "\toperacion: \"" << ins.operacion << "\",\n";
    cout << "\tregistros: [";
    for (size_t i = 0; i < ins.registros.size(); ++i)
        cout << (i ? ", " : "") << "\"" << ins.registros[i] << "\"";
    cout << "],\n";
    cout << "\tdireccion: " << (ins.tieneDireccion ? ins.direccion : "null") << "\n";
    cout << "}\n";
}

void procesar(const string& linea, bool mostrarTraza) {
    cout << "\nENTRADA\n" << linea << "\n\n";

    cout << "VALIDACIÓN MEDIANTE AFD\n";
    ResultadoAFD r = AFD::analizar(linea);

    if (!r.valido) {
        cout << "Instrucción inválida: " << r.error << ".\n";
        if (r.posicionError > 0)
            cout << "Posición del error: carácter " << r.posicionError << ".\n";
        if (mostrarTraza) {
            cout << "\nRECORRIDO DEL AFD\n";
            for (const string& t : r.traza) cout << t << "\n";
        }
        cout << "\nNo se construye el objeto instrucción.\n";
        cout << "No se generan microoperaciones.\n";
        return;
    }
    cout << "Instrucción válida.\n";

    if (mostrarTraza) {
        cout << "\nRECORRIDO DEL AFD\n";
        for (const string& t : r.traza) cout << t << "\n";
    }

    cout << "\nTOKENS RECONOCIDOS\n";
    for (const Token& t : r.tokens)
        cout << t.tipo << "(\"" << t.lexema << "\")\n";

    Instruccion ins = construirInstruccion(r.tokens);

    cout << "\nCOMPONENTES IDENTIFICADOS\n";
    imprimirComponentes(ins);

    cout << "\nOBJETO INSTRUCCIÓN\n";
    imprimirObjeto(ins);

    cout << "\nMICROOPERACIONES GENERADAS POR MOORE\n";
    MaquinaMoore moore(ins);
    int n = 0;
    while (!moore.terminada())
        cout << ++n << ". " << moore.siguientePaso() << "\n";
    cout << "\nGeneración terminada.\n";
}

void correrPruebas(bool mostrarTraza) {
    const vector<string> validas   = {"MOV AL, 6", "MOV BL, 7", "ADD AL, BL", "STO 8", "END"};
    const vector<string> invalidas = {"MOV AX, 6", "MOV AL 6", "ADD AL,", "STO", "END 8"};

    cout << "\n==================== ENTRADAS VÁLIDAS ====================\n";
    for (const string& s : validas) procesar(s, mostrarTraza);
    cout << "\n=================== ENTRADAS INVÁLIDAS ===================\n";
    for (const string& s : invalidas) procesar(s, mostrarTraza);
}

// ====================================================================================================
// Programa principal
// ====================================================================================================

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);    // para mostrar ← y acentos
#endif
    bool traza = false;

    cout << "\033[1;32mAnalizador del ISA (AFD + Máquina de Moore)\033[0m" << endl;
    cout << "\033[1;34mEscribe una instrucción (MOV R, d | ADD AL, BL | STO d | END)\033[0m" << endl;
    cout << "\033[1;34mComandos: PRUEBAS (corre las 10 entradas de la práctica), TRAZA (activa/desactiva el recorrido de estados del AFD), SALIR\033[0m" << endl;

    string linea;
    while (true) {
        cout << "\n\033[1;31m> \033[0m";
        if (!getline(cin, linea)) break;
        if (!linea.empty() && linea.back() == '\r') linea.pop_back();   // solo se quita el retorno de carro

        if (linea == "SALIR")   break;
        if (linea == "PRUEBAS") { correrPruebas(traza); continue; }
        if (linea == "TRAZA")   {
            traza = !traza;
            cout << "\033[1;34mRecorrido de estados: " << (traza ? "activado" : "desactivado") << "\033[0m" << endl;
            continue;
        }
        procesar(linea, traza);
    }
    return 0;
}
