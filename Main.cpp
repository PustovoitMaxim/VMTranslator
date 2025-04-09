#include <iostream>
#include <fstream>
#include <vector>
#include <filesystem>
#include <sstream>
#include <unordered_map>

namespace fs = std::filesystem;

enum CommandType {
    C_ARITHMETIC,
    C_PUSH,
    C_POP,
    C_LABEL,
    C_GOTO,
    C_IF,
    C_FUNCTION,
    C_RETURN,
    C_CALL,
    C_ERROR
};

class Parser {
private:
    std::ifstream file;
    std::string currentCommand;
    std::vector<std::string> tokens;

public:
    // Constructor
    Parser(const std::string& filename) {
        file.open(filename);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open file: " + filename);
        }
    }

    //Destructor
    ~Parser() {
        if (file.is_open()) file.close();
    }
    bool hasSysInit(const std::vector<std::string>& vmFiles) {
        for (const auto& file : vmFiles) {
            std::ifstream vmFile(file);
            if (!vmFile.is_open()) continue;

            std::string line;
            while (std::getline(vmFile, line)) {
                size_t commentPos = line.find("//");
                if (commentPos != std::string::npos) {
                    line = line.substr(0, commentPos);
                }
                line = trim(line); // Вызов приватного метода
                if (line.find("function Sys.init") != std::string::npos) {
                    vmFile.close();
                    return true;
                }
            }
            vmFile.close();
        }
        return false;
    }
    // Check if there are any commands left
    bool hasMoreCommands() {
        return !file.eof();
    }

    // split and clean line
    void advance() {
        std::string line;
        do {
            if (!std::getline(file, line)) {
                currentCommand = "";
                tokens.clear(); // Очистка токенов
                return;
            }
            // Удаление комментариев и лишних пробелов
            size_t commentPos = line.find("//");
            if (commentPos != std::string::npos) {
                line = line.substr(0, commentPos);
            }
            line = trim(line);
        } while (line.empty());

        currentCommand = line;
        tokens = split(line);
    }

    //Define command type
    CommandType commandType() {
        if (tokens.empty()) return C_ERROR;
        const std::string& cmd = tokens[0];
        if (cmd == "push") return C_PUSH;
        if (cmd == "pop") return C_POP;
        if (cmd == "label") return C_LABEL;
        if (cmd == "goto") return C_GOTO;
        if (cmd == "if-goto") return C_IF;
        if (cmd == "function") return C_FUNCTION;
        if (cmd == "call") return C_CALL;
        if (cmd == "return") return C_RETURN;
        if (isArithmetic(cmd)) return C_ARITHMETIC;
        return C_ERROR;
    }

    // First argument
    std::string arg1() {
        if (commandType() == C_ARITHMETIC) return tokens[0];
        if (tokens.size() >= 2) return tokens[1];
        return "";
    }

    //Second argument
    int arg2() {
        if (tokens.size() >= 3) return std::stoi(tokens[2]);
        return -1;
    }
private:
        std::vector<std::string> split(const std::string& s) {
        std::vector<std::string> tokens;
        std::stringstream ss(s);
        std::string token;
        while (ss >> token) {
            tokens.push_back(token);
        }
        return tokens;
    }

    std::string trim(const std::string& s) {
        size_t start = s.find_first_not_of(" \t");
        size_t end = s.find_last_not_of(" \t");
        if (start == std::string::npos) return "";
        return s.substr(start, end - start + 1);
    }

    //define if the string is arithmetic
    bool isArithmetic(const std::string& cmd) {
        static const std::unordered_map<std::string, bool> arithmetic = {
            {"add", true}, {"sub", true}, {"neg", true},
            {"eq", true}, {"gt", true}, {"lt", true},
            {"and", true}, {"or", true}, {"not", true}
        };
        return arithmetic.count(cmd);
    }

};

//Generates Asm Code
class CodeWriter {
private:
    std::ofstream output;
    std::string currentFunction;
    std::string filename;
    int labelCounter = 0;

public:
    // Constructor
    CodeWriter(const std::string& outputFile) {
        output.open(outputFile);
        if (!output.is_open()) {
            throw std::runtime_error("Failed to open output file: " + outputFile);
        }
    }
    // Destructor
    ~CodeWriter() {
        if (output.is_open()) output.close();
    }
    // Sets file name
    void setFileName(const std::string& name) {
        filename = fs::path(name).stem().string();
    }
    // Write BoostStrap code
    void writeInit() {
        output << "// Bootstrap initialization\n";
        output << "@256\nD=A\n@SP\nM=D\n";
        writeCall("Sys.init", 0);
    }
    //Write Binary Operation
    void writeArithmetic(const std::string& command) {
        output << "// " << command << "\n";
        if (command == "add" || command == "sub" || command == "and" || command == "or") {
            binaryOp(command);
        }
        else if (command == "neg" || command == "not") {
            unaryOp(command);
        }
        else {
            compareOp(command);
        }
    }
    //Write push/pop operations
    void writePushPop(CommandType type, const std::string& segment, int index) {
        output << "// " << (type == C_PUSH ? "push" : "pop") << " " << segment << " " << index << "\n";
        if (type == C_PUSH) {
            push(segment, index);
        }
        else {
            pop(segment, index);
        }
    }
    // Write labels
    void writeLabel(const std::string& label) {
        output << "// label " << label << "\n";
        if (currentFunction.empty()) {
            output << "(" << label << ")\n"; // Глобальная метка
        }
        else {
            output << "(" << currentFunction << "$" << label << ")\n";
        }
    }
    // Write goto operation
    void writeGoto(const std::string& label) {
        output << "// goto " << label << "\n";
        if (currentFunction.empty()) {
            output << "@" << label << "\n0;JMP\n";
        }
        else {
            output << "@" << currentFunction << "$" << label << "\n0;JMP\n";
        }
    }
    // write if operation
    void writeIf(const std::string& label) {
        output << "// if-goto " << label << "\n";
        output << "@SP\nAM=M-1\nD=M\n";

        if (currentFunction.empty()) {
            output << "@" << label << "\nD;JNE\n";
        }
        else {
            output << "@" << currentFunction << "$" << label << "\nD;JNE\n";
        }
    }
    // Creates function definition
    void writeFunction(const std::string& functionName, int numVars) {
        output << "// function " << functionName << " " << numVars << "\n";
        currentFunction = functionName;
        output << "(" << functionName << ")\n";
        for (int i = 0; i < numVars; i++) {
            push("constant", 0);
        }
    }
    // Generates call function code
    void writeCall(const std::string& functionName, int numArgs) {
        output << "// call " << functionName << " " << numArgs << "\n"; 
        std::string returnLabel = currentFunction + "$ret." + std::to_string(labelCounter++);
        pushValue(returnLabel); // Сохраняем адрес возврата
        pushSegment("LCL");
        pushSegment("ARG");
        pushSegment("THIS");
        pushSegment("THAT");
        output << "@SP\nD=M\n@" << (numArgs + 5) << "\nD=D-A\n@ARG\nM=D\n";
        output << "@SP\nD=M\n@LCL\nM=D\n";
        output << "@" << functionName << "\n0;JMP\n";
        output << "(" << returnLabel << ")\n";
    }
    // Generates Return code
    void writeReturn() {
        output << "// return\n";
        output << "@LCL\nD=M\n@R13\nM=D\n";             // FRAME = LCL
        output << "@5\nD=D-A\nA=D\nD=M\n@R14\nM=D\n";   // RET = *(FRAME-5) 
        output << "@SP\nAM=M-1\nD=M\n@ARG\nA=M\nM=D\n"; // *ARG = pop()
        output << "@ARG\nD=M+1\n@SP\nM=D\n";            // SP = ARG+1
        restoreSegment("THAT", 1);
        restoreSegment("THIS", 2);
        restoreSegment("ARG", 3);
        restoreSegment("LCL", 4);
        output << "@R14\nA=M\n0;JMP\n";                 // goto RET
    }
private:
    // Generates binary code operations
    void binaryOp(const std::string& op) {
        output << "@SP\nAM=M-1\nD=M\nA=A-1\n";
        if (op == "add") output << "M=D+M\n";
        else if (op == "sub") output << "M=M-D\n";
        else if (op == "and") output << "M=D&M\n";
        else if (op == "or") output << "M=D|M\n";
    }
    // Generates unary code operations
    void unaryOp(const std::string& op) {
        output << "@SP\nA=M-1\n";
        if (op == "neg") output << "M=-M\n";
        else if (op == "not") output << "M=!M\n";
    }
    // Generates comparison
    void compareOp(const std::string& op) {
        std::string label = "COMP_" + std::to_string(labelCounter++);
        output << "@SP\nAM=M-1\nD=M\nA=A-1\nD=M-D\n";
        output << "@" << label << "_TRUE\n";
        output << "D;J" << getJumpCondition(op) << "\n";
        output << "@SP\nA=M-1\nM=0\n@" << label << "_END\n0;JMP\n";
        output << "(" << label << "_TRUE)\n@SP\nA=M-1\nM=-1\n";
        output << "(" << label << "_END)\n";
    }
    // Generates Jump conditions
    std::string getJumpCondition(const std::string& op) {
        if (op == "eq") return "EQ";
        if (op == "gt") return "GT";
        if (op == "lt") return "LT";
        return "";
    }
    // Generates push commands
    void push(const std::string& segment, int index) {
        if (segment == "constant") {
        output << "@" << index << "\nD=A\n";
    }
    else if (segment == "static") {
        output << "@" << filename << "." << index << "\nD=M\n";
    }
    else if (segment == "pointer") { // Добавлено!
        output << "@" << (index == 0 ? "THIS" : "THAT") << "\nD=M\n";
    }
    else {
        resolveSegmentAddress(segment, index);
        output << "D=M\n";
    }
    output << "@SP\nA=M\nM=D\n@SP\nM=M+1\n";
    }
    // Generates pop commands
    void pop(const std::string& segment, int index) {
        if (segment == "static") {
            output << "@SP\nAM=M-1\nD=M\n@" << filename << "." << index << "\nM=D\n";
            return;
        }
        if (segment == "pointer") {
            output << "@SP\nAM=M-1\nD=M\n@" << (index == 0 ? "THIS" : "THAT") << "\nM=D\n";
            return;
        }
        resolveSegmentAddress(segment, index);
        output << "D=A\n@R13\nM=D\n@SP\nAM=M-1\nD=M\n@R13\nA=M\nM=D\n";
    }
    // generates segment address
    void resolveSegmentAddress(const std::string& segment, int index) {
        static std::unordered_map<std::string, std::string> segmentMap = {
            {"local", "LCL"}, {"argument", "ARG"},
            {"this", "THIS"}, {"that", "THAT"},
            {"temp", "5"}, {"pointer", "3"}
        };
        std::string base = segmentMap[segment];
        if (segment == "temp" || segment == "pointer") {
            output << "@" << base << "\nD=A\n@" << index << "\nA=D+A\n";
        }
        else {
            output << "@" << base << "\nD=M\n@" << index << "\nA=D+A\n";
        }
    }
    // generates push value 
    void pushValue(const std::string& value) {
        output << "@" << value << "\nD=A\n@SP\nA=M\nM=D\n@SP\nM=M+1\n";
    }
    // generates push value with a segment name
    void pushSegment(const std::string& segment) {
        output << "@" << segment << "\nD=M\n@SP\nA=M\nM=D\n@SP\nM=M+1\n";
    }
    // restore value with a segment name
    void restoreSegment(const std::string& segment, int offset) {
        output << "@R13\nD=M\n@" << offset << "\nA=D-A\nD=M\n@" << segment << "\nM=D\n";
    }
};

// Main section
int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <input.vm or directory>\n";
        return 1;
    }

    std::string inputPath = argv[1];
    std::vector<std::string> vmFiles;
    //  looking for a .vm files in directory
    if (fs::is_directory(inputPath)) {
        for (const auto& entry : fs::directory_iterator(inputPath)) {
            // adding file in vmFiles vector if it's .vm file
            if (entry.path().extension() == ".vm") {
                vmFiles.push_back(entry.path().string());
            }
        }
    }
    // adding single file in vmFiles vector
    else if (fs::path(inputPath).extension() == ".vm") {
        vmFiles.push_back(inputPath);
    }
    else {
        std::cerr << "Invalid input. Provide a .vm file or directory.\n";
        return 1;
    }
    // creating a outputFile name
    std::string outputFile;

    if (fs::is_directory(inputPath)) {
        fs::path dirPath = fs::path(inputPath);
        std::string dirName;

        // Нормализация пути: удаление завершающего слеша
        std::string pathStr = dirPath.string();
        if (!pathStr.empty() && (pathStr.back() == '/' || pathStr.back() == '\\')) {
            pathStr.pop_back();
        }
        fs::path normalizedPath(pathStr);

        // Получаем имя директории из нормализованного пути
        dirName = normalizedPath.filename().string();

        // Если имя директории всё ещё пустое (например, корневой путь "/")
        if (dirName.empty()) {
            dirName = "output"; // Универсальное имя по умолчанию
        }

        outputFile = (dirPath / dirName).replace_extension(".asm").string();
    }
    else {
        outputFile = fs::path(inputPath).replace_extension(".asm").string();
    }
    CodeWriter writer(outputFile);

    // Проверяем наличие Sys.init в VM-файлах
    Parser sysInitChecker(vmFiles[0]); // Создаём парсер для проверки
    bool hasSysInit = sysInitChecker.hasSysInit(vmFiles);

    // Если Sys.init найден, добавляем код инициализации
    if (hasSysInit) {
        writer.writeInit();
    }

    for (const auto& file : vmFiles) {
        Parser parser(file);
        writer.setFileName(file);

        while (parser.hasMoreCommands()) {
            parser.advance();
            CommandType type = parser.commandType();
            if (type == C_ERROR) continue;

            switch (type) {
            case C_ARITHMETIC:
                writer.writeArithmetic(parser.arg1());
                break;
            case C_PUSH:
            case C_POP:
                writer.writePushPop(type, parser.arg1(), parser.arg2());
                break;
            case C_LABEL:
                writer.writeLabel(parser.arg1());
                break;
            case C_GOTO:
                writer.writeGoto(parser.arg1());
                break;
            case C_IF:
                writer.writeIf(parser.arg1());
                break;
            case C_FUNCTION:
                writer.writeFunction(parser.arg1(), parser.arg2());
                break;
            case C_CALL:
                writer.writeCall(parser.arg1(), parser.arg2());
                break;
            case C_RETURN:
                writer.writeReturn();
                break;
            default:
                break;
            }
        }
    }

    return 0;
}