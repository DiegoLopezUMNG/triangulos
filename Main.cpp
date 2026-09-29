// =====================================================================================
//  PIRÁMIDE DE 3 TRIÁNGULOS CON TEXTURA, ROTANDO EN SU PROPIO EJE + CÁMARA TIPO FPS
//  Librerías: GLFW (ventana/input), GLAD (cargador de OpenGL), GLM (matemáticas),
//             stb_image (cargar imágenes para la textura)
//  Sigue el esquema de tus PDFs: CONSTRUCT (contexto de ventana) -> buffers (VAO/VBO/EBO)
//  -> shaders (vertex + fragment) -> CONTINUUM (render loop) enviando la MVP como uniform.
// =====================================================================================

// ------------------------------- 1. INCLUDES -----------------------------------------

#include <glad/glad.h>                      // GLAD: debe ir ANTES de GLFW; carga los punteros a las funciones de OpenGL (glGenBuffers, etc.)
#include <GLFW/glfw3.h>                     // GLFW: crea la ventana, el contexto OpenGL y maneja teclado/mouse

#include <glm/glm.hpp>                      // GLM: tipos matemáticos base (vec2, vec3, mat4...)
#include <glm/gtc/matrix_transform.hpp>     // GLM: funciones para construir matrices (translate, rotate, lookAt, perspective)
#include <glm/gtc/type_ptr.hpp>             // GLM: value_ptr(), convierte una matriz GLM en un puntero float* que OpenGL entiende

#include <iostream>                         // std::cout / std::cerr para imprimir mensajes y errores en la consola

#define STB_IMAGE_IMPLEMENTATION            // Le dice a stb_image.h que incluya aquí la implementación (SOLO en un .cpp, una vez)
#include <stb_image.h>                      // stb_image: carga imágenes (jpg, png, bmp...) a memoria para usarlas como textura

// ------------------------------- 2. CONSTANTES Y ESTADO GLOBAL -------------------------

const unsigned int SCR_WIDTH = 1000;        // Ancho inicial de la ventana en píxeles
const unsigned int SCR_HEIGHT = 700;        // Alto inicial de la ventana en píxeles

// ---- Estado de la cámara (sistema de referencia de la cámara) ----
glm::vec3 cameraPos = glm::vec3(0.0f, 0.0f, 3.0f);   // Posición de la cámara en el mundo: 3 unidades hacia +Z (alejada del origen)
glm::vec3 cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);  // Dirección hacia donde mira la cámara: hacia -Z (hacia la pirámide)
glm::vec3 cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);   // Vector "arriba" del mundo: eje Y positivo (evita que la cámara se voltee)

// ---- Estado del mouse ----
bool  firstMouse = true;                    // true hasta recibir el primer movimiento (evita un "salto" brusco de cámara al inicio)
float lastX = SCR_WIDTH / 2.0f;             // Última posición X conocida del mouse (empieza en el centro de la ventana)
float lastY = SCR_HEIGHT / 2.0f;            // Última posición Y conocida del mouse (empieza en el centro de la ventana)
float yaw = -90.0f;                       // Giro horizontal en grados; -90 hace que la cámara mire hacia -Z (yaw 0 miraría a +X)
float pitch = 0.0f;                         // Giro vertical en grados; 0 = mirando de frente (ni arriba ni abajo)

// ---- Tiempo ----
float deltaTime = 0.0f;                     // Tiempo (segundos) que tardó el último frame; sirve para que el movimiento no dependa de los FPS
float lastFrame = 0.0f;                     // Instante (segundos) en que empezó el frame anterior

// ------------------------------- 3. CÓDIGO DE LOS SHADERS ------------------------------
// Los shaders se escriben en GLSL y corren en la GPU. Aquí van como texto dentro del C++.
// R"( ... )" es un "raw string": permite escribir texto en varias líneas sin comillas ni \n.

// ---- VERTEX SHADER: se ejecuta UNA VEZ POR CADA VÉRTICE ----
const char* vertexShaderSource = R"(
#version 330 core                           // Versión de GLSL = OpenGL 3.3, perfil core (sin funciones antiguas)

layout (location = 0) in vec3 aPos;         // Entrada 0: posición (x,y,z) del vértice, viene del VBO (atributo 0)
layout (location = 1) in vec2 aTexCoord;    // Entrada 1: coordenada de textura (u,v) del vértice, viene del VBO (atributo 1)

out vec2 TexCoord;                          // Salida hacia el fragment shader: la coordenada de textura (se interpola en la rasterización)

uniform mat4 MVP;                           // Uniform: matriz Model*View*Projection calculada en la CPU (igual para todos los vértices)

void main()                                 // Función principal del shader, se llama una vez por vértice
{
    gl_Position = MVP * vec4(aPos, 1.0);    // Transforma la posición local a coordenadas de recorte; w=1.0 porque es un punto
    TexCoord = aTexCoord;                   // Pasa la coordenada de textura sin cambios al siguiente stage
}
)";

// ---- FRAGMENT SHADER: se ejecuta UNA VEZ POR CADA FRAGMENTO (píxel candidato) ----
const char* fragmentShaderSource = R"(
#version 330 core                           // Misma versión que el vertex shader

in vec2 TexCoord;                           // Entrada: coordenada de textura ya interpolada para este fragmento
out vec4 fragColor;                         // Salida: color final (RGBA) de este fragmento

uniform sampler2D texture1;                 // Uniform: "sampler" que representa la textura 2D enlazada en la unidad 0

void main()                                 // Función principal, se llama una vez por fragmento
{
    fragColor = texture(texture1, TexCoord);// Lee el color del texel correspondiente a (u,v) en la textura y lo usa como color final
}
)";

// ------------------------------- 4. DECLARACIÓN DE FUNCIONES ---------------------------

void framebuffer_size_callback(GLFWwindow* window, int width, int height);  // Se llama cuando cambia el tamaño de la ventana
void mouse_callback(GLFWwindow* window, double xpos, double ypos);          // Se llama cada vez que se mueve el mouse
void processInput(GLFWwindow* window);                                      // Lee el teclado (WASD y ESC) cada frame
unsigned int compileShader(unsigned int type, const char* source);          // Compila un shader y verifica errores
unsigned int createShaderProgram(const char* vsSource, const char* fsSource); // Compila, enlaza y devuelve el programa de shaders
unsigned int loadTexture(const char* path);                                 // Carga una textura desde archivo (o genera un tablero si falla)

// ------------------------------- 5. FUNCIÓN PRINCIPAL ----------------------------------

int main()                                  // Punto de entrada del programa
{
    // ===================== CONSTRUCT: ventana y contexto =====================

    glfwInit();                                                     // Inicializa GLFW; hay que llamarlo antes que cualquier otra función de GLFW
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);                  // Pide un contexto OpenGL versión mayor = 3
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);                  // Versión menor = 3 (o sea, OpenGL 3.3)
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // Perfil core: solo funciones modernas (obligatorio usar shaders)

    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Piramide OpenGL", NULL, NULL); // Crea la ventana con título; NULL,NULL = ni pantalla completa ni contexto compartido
    if (window == NULL)                                             // Si la creación falló, window es NULL
    {
        std::cerr << "Error: no se pudo crear la ventana GLFW" << std::endl; // Mensaje de error en la consola
        glfwTerminate();                                            // Libera los recursos de GLFW
        return -1;                                                  // Termina el programa con código de error
    }
    glfwMakeContextCurrent(window);                                 // Hace que el contexto OpenGL de esta ventana sea el activo en este hilo
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback); // Registra la función que ajusta el viewport al redimensionar
    glfwSetCursorPosCallback(window, mouse_callback);               // Registra la función que recibe la posición del mouse
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);    // Oculta y "captura" el cursor: movimiento infinito, como en un videojuego

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))       // GLAD obtiene las direcciones de las funciones de OpenGL usando GLFW
    {
        std::cerr << "Error: no se pudo inicializar GLAD" << std::endl; // Si falla, no podemos usar OpenGL
        return -1;                                                  // Salimos con error
    }

    glEnable(GL_DEPTH_TEST);                // Activa el z-buffer: los fragmentos más cercanos tapan a los lejanos (superficies visibles)

    // ===================== SHADERS =====================

    unsigned int shaderProgram = createShaderProgram(vertexShaderSource, fragmentShaderSource); // Compila y enlaza ambos shaders; devuelve el ID del programa

    // ===================== DATOS DE LA PIRÁMIDE =====================
    // Base imaginaria: triángulo equilátero en el plano XZ, centrado en el origen (y = -0.5).
    // Vértice superior (ápice): (0, 0.5, 0), justo sobre el eje Y -> así rota sobre su propio eje.
    // Cada cara lateral es un triángulo: [esquina base A, esquina base B, ápice].
    // Cada vértice lleva: x, y, z, u, v  (5 floats).
    // Hay 3 triángulos = 3 caras laterales (la base queda abierta, como pediste: 3 triángulos).

    float vertices[] = {
        // ---- Cara 1 (frontal): esquinas A y B + ápice ----
        //   x       y      z       u     v
        -0.50f, -0.5f, -0.29f,   0.0f, 0.0f,   // Vértice 0: esquina A de la base, abajo-izquierda de la textura
         0.50f, -0.5f, -0.29f,   1.0f, 0.0f,   // Vértice 1: esquina B de la base, abajo-derecha de la textura
         0.00f,  0.5f,  0.00f,   0.5f, 1.0f,   // Vértice 2: ápice, arriba-centro de la textura

         // ---- Cara 2: esquinas B y C + ápice ----
          0.50f, -0.5f, -0.29f,   0.0f, 0.0f,   // Vértice 3: esquina B otra vez (cada cara necesita sus propias coordenadas UV)
          0.00f, -0.5f,  0.58f,   1.0f, 0.0f,   // Vértice 4: esquina C de la base
          0.00f,  0.5f,  0.00f,   0.5f, 1.0f,   // Vértice 5: ápice

          // ---- Cara 3: esquinas C y A + ápice ----
           0.00f, -0.5f,  0.58f,   0.0f, 0.0f,   // Vértice 6: esquina C
          -0.50f, -0.5f, -0.29f,   1.0f, 0.0f,   // Vértice 7: esquina A
           0.00f,  0.5f,  0.00f,   0.5f, 1.0f    // Vértice 8: ápice
    };

    unsigned int indices[] = {              // Índices del EBO: indican en qué orden usar los vértices del VBO
        0, 1, 2,                            // Triángulo 1: vértices 0, 1, 2
        3, 4, 5,                            // Triángulo 2: vértices 3, 4, 5
        6, 7, 8                             // Triángulo 3: vértices 6, 7, 8
    };

    // ===================== BUFFERS: VAO, VBO, EBO =====================

    unsigned int VAO, VBO, EBO;             // IDs (números) con los que OpenGL identifica cada objeto

    glGenVertexArrays(1, &VAO);             // Crea 1 VAO (Vertex Array Object): guarda la configuración de cómo leer los vértices
    glGenBuffers(1, &VBO);                  // Crea 1 VBO (Vertex Buffer Object): memoria en la GPU para los datos de los vértices
    glGenBuffers(1, &EBO);                  // Crea 1 EBO (Element Buffer Object): memoria en la GPU para los índices

    glBindVertexArray(VAO);                 // Activa el VAO: todo lo que configuremos ahora quedará guardado dentro de él

    glBindBuffer(GL_ARRAY_BUFFER, VBO);     // Activa el VBO como buffer de atributos de vértices
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW); // Copia el arreglo 'vertices' a la GPU; STATIC = no cambiará

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO); // Activa el EBO (queda registrado dentro del VAO activo)
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW); // Copia los índices a la GPU

    // Atributo 0 = posición (3 floats). Coincide con "layout (location = 0)" del vertex shader.
    glVertexAttribPointer(0,                // 0: número de atributo (location del shader)
        3,                // 3: cantidad de componentes (x, y, z)
        GL_FLOAT,         // Tipo de dato de cada componente
        GL_FALSE,         // No normalizar (los datos ya son floats)
        5 * sizeof(float),// Stride: bytes entre el inicio de un vértice y el siguiente (5 floats = x,y,z,u,v)
        (void*)0);        // Offset: la posición empieza en el byte 0 del vértice
    glEnableVertexAttribArray(0);           // Habilita el atributo 0 (por defecto están deshabilitados)

    // Atributo 1 = coordenadas de textura (2 floats). Coincide con "layout (location = 1)".
    glVertexAttribPointer(1,                // 1: número de atributo (location del shader)
        2,                // 2: cantidad de componentes (u, v)
        GL_FLOAT,         // Tipo de dato
        GL_FALSE,         // No normalizar
        5 * sizeof(float),// Stride: igual, 5 floats por vértice
        (void*)(3 * sizeof(float))); // Offset: (u,v) empiezan después de los 3 floats de la posición
    glEnableVertexAttribArray(1);           // Habilita el atributo 1

    glBindVertexArray(0);                   // Desactiva el VAO para no modificarlo por accidente (la config queda guardada en él)

    // ===================== TEXTURA =====================

    unsigned int texture = loadTexture("textura.jpg"); // Carga "textura.jpg" (ponla en la carpeta del proyecto); si no existe, usa un tablero

    glUseProgram(shaderProgram);            // Activa el programa de shaders (necesario antes de asignar uniforms)
    glUniform1i(glGetUniformLocation(shaderProgram, "texture1"), 0); // Le dice al sampler "texture1" que lea de la unidad de textura 0

    // Ubicación (ID) del uniform MVP: se pide una sola vez, fuera del loop, porque no cambia
    int mvpLocation = glGetUniformLocation(shaderProgram, "MVP");

    // ===================== CONTINUUM: RENDER LOOP =====================

    while (!glfwWindowShouldClose(window))  // Repite mientras nadie haya pedido cerrar la ventana
    {
        float currentFrame = (float)glfwGetTime();  // Segundos transcurridos desde que se inició GLFW
        deltaTime = currentFrame - lastFrame;       // Duración del frame anterior = ahora - momento del frame previo
        lastFrame = currentFrame;                   // Guarda el momento actual para calcular el próximo deltaTime

        processInput(window);                       // Lee WASD/ESC y mueve la cámara

        glClearColor(0.1f, 0.1f, 0.15f, 1.0f);      // Define el color de fondo (azul oscuro) en RGBA
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); // Borra el color y el z-buffer del frame anterior

        // ---- Matriz MODEL: posiciona/rota el objeto en el mundo ----
        glm::mat4 model = glm::mat4(1.0f);          // Matriz identidad (no hace nada); punto de partida
        model = glm::rotate(model,                  // Aplica una rotación a la matriz
            currentFrame,           // Ángulo en radianes: crece con el tiempo -> giro continuo (~57°/s)
            glm::vec3(0.0f, 1.0f, 0.0f)); // Eje de rotación: Y (el eje vertical que pasa por el ápice y el centro de la base)

        // ---- Matriz VIEW: la cámara ----
        glm::mat4 view = glm::lookAt(cameraPos,                 // Dónde está el ojo
            cameraPos + cameraFront,   // Punto al que mira (posición + dirección de vista)
            cameraUp);                 // Vector que define "arriba"

        // ---- Matriz PROJECTION: perspectiva ----
        int width, height;                                      // Variables para el tamaño actual de la ventana
        glfwGetFramebufferSize(window, &width, &height);        // Obtiene el tamaño real en píxeles (por si se redimensionó)
        if (height == 0) height = 1;                            // Evita división entre cero al minimizar la ventana
        glm::mat4 projection = glm::perspective(glm::radians(45.0f),        // Campo de visión vertical: 45° (convertido a radianes)
            (float)width / (float)height, // Relación de aspecto = ancho / alto
            0.1f,           // Plano cercano: nada más cerca de 0.1 se dibuja
            100.0f);        // Plano lejano: nada más lejos de 100 se dibuja

        glm::mat4 MVP = projection * view * model;  // Combina todo; el orden importa: primero model, luego view, luego projection (se lee de derecha a izquierda)

        glUseProgram(shaderProgram);                // Activa el programa de shaders para dibujar
        glUniformMatrix4fv(mvpLocation,             // Ubicación del uniform MVP en el shader
            1,                       // Cantidad de matrices que enviamos: 1
            GL_FALSE,                // No transponer (GLM ya usa el orden de columnas que OpenGL espera)
            glm::value_ptr(MVP));    // Puntero a los 16 floats de la matriz

        glActiveTexture(GL_TEXTURE0);               // Selecciona la unidad de textura 0
        glBindTexture(GL_TEXTURE_2D, texture);      // Enlaza nuestra textura a esa unidad (el sampler texture1 la leerá)

        glBindVertexArray(VAO);                     // Activa el VAO: recupera de golpe VBO, EBO y formato de atributos
        glDrawElements(GL_TRIANGLES,                // Dibuja triángulos
            9,                           // Cantidad de índices a usar (3 triángulos x 3 vértices)
            GL_UNSIGNED_INT,             // Tipo de los índices (unsigned int)
            0);                          // Offset dentro del EBO: empezar en el primer índice

        glfwSwapBuffers(window);                    // Intercambia el buffer trasero (recién dibujado) con el delantero (visible): evita parpadeos
        glfwPollEvents();                           // Procesa eventos pendientes (teclado, mouse, cerrar ventana) y dispara los callbacks
    }

    // ===================== LIMPIEZA =====================

    glDeleteVertexArrays(1, &VAO);          // Libera el VAO en la GPU
    glDeleteBuffers(1, &VBO);               // Libera el VBO
    glDeleteBuffers(1, &EBO);               // Libera el EBO
    glDeleteTextures(1, &texture);          // Libera la textura
    glDeleteProgram(shaderProgram);         // Libera el programa de shaders

    glfwTerminate();                        // Cierra GLFW y destruye la ventana
    return 0;                               // Fin normal del programa
}

// ------------------------------- 6. IMPLEMENTACIÓN DE FUNCIONES ------------------------

// ---- Ajusta el área de dibujo cuando cambia el tamaño de la ventana ----
void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    glViewport(0, 0, width, height);        // Área de la ventana donde se dibuja: desde (0,0) hasta (ancho,alto)
}

// ---- Teclado: mueve la cámara con WASD y cierra con ESC ----
void processInput(GLFWwindow* window)
{
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)  // ¿Está presionada la tecla ESC?
        glfwSetWindowShouldClose(window, true);             // Marca la ventana para cerrarse (el while termina)

    float cameraSpeed = 2.5f * deltaTime;   // Velocidad: 2.5 unidades por segundo (multiplicar por deltaTime lo hace independiente de los FPS)

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)       // W: adelante
        cameraPos += cameraSpeed * cameraFront;             // Avanza en la dirección en la que mira la cámara
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)       // S: atrás
        cameraPos -= cameraSpeed * cameraFront;             // Retrocede (dirección opuesta a la vista)
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)       // A: izquierda
        cameraPos -= glm::normalize(glm::cross(cameraFront, cameraUp)) * cameraSpeed; // cross(front, up) = vector "derecha"; restarlo = izquierda; normalize para longitud 1
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)       // D: derecha
        cameraPos += glm::normalize(glm::cross(cameraFront, cameraUp)) * cameraSpeed; // Sumar el vector "derecha" = moverse a la derecha
}

// ---- Mouse: convierte el movimiento (x, y) en giro de la cámara (actualiza cameraFront, que alimenta a lookAt) ----
void mouse_callback(GLFWwindow* window, double xpos, double ypos)
{
    if (firstMouse)                         // En el primer evento no hay posición previa válida
    {
        lastX = (float)xpos;                // Toma la posición actual como la "última"
        lastY = (float)ypos;                // Ídem para Y
        firstMouse = false;                 // Ya no es el primer movimiento
    }

    float xoffset = (float)xpos - lastX;    // Cuánto se movió el mouse en X desde el último evento (derecha = positivo)
    float yoffset = lastY - (float)ypos;    // Cuánto se movió en Y; invertido porque en pantalla Y crece hacia abajo (arriba = positivo)
    lastX = (float)xpos;                    // Guarda la posición actual para el próximo evento
    lastY = (float)ypos;                    // Ídem para Y

    float sensitivity = 0.1f;               // Sensibilidad: grados de giro por píxel movido (súbela o bájala a tu gusto)
    xoffset *= sensitivity;                 // Escala el desplazamiento horizontal
    yoffset *= sensitivity;                 // Escala el desplazamiento vertical

    yaw += xoffset;                       // Acumula el giro horizontal (izquierda/derecha)
    pitch += yoffset;                       // Acumula el giro vertical (arriba/abajo)

    if (pitch > 89.0f)  pitch = 89.0f;      // Límite superior: evita que la cámara pase de mirar recto hacia arriba y se voltee
    if (pitch < -89.0f) pitch = -89.0f;     // Límite inferior: igual hacia abajo

    glm::vec3 front;                                                            // Nuevo vector de dirección
    front.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));                // Componente X: trigonometría esférica -> cartesiana
    front.y = sin(glm::radians(pitch));                                         // Componente Y: solo depende del pitch (subir/bajar)
    front.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));                // Componente Z: depende de yaw y pitch
    cameraFront = glm::normalize(front);                                        // Normaliza (longitud 1) y guarda; lookAt usará este valor
}

// ---- Compila un shader (vertex o fragment) y muestra errores si los hay ----
unsigned int compileShader(unsigned int type, const char* source)
{
    unsigned int shader = glCreateShader(type);     // Crea un objeto shader del tipo pedido (GL_VERTEX_SHADER o GL_FRAGMENT_SHADER)
    glShaderSource(shader, 1, &source, NULL);       // Le pasa el código fuente: 1 cadena, NULL = termina en '\0'
    glCompileShader(shader);                        // Compila el código GLSL en la GPU

    int success;                                    // Aquí se guarda si la compilación funcionó
    char infoLog[512];                              // Buffer para el mensaje de error
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success); // Consulta el estado de la compilación
    if (!success)                                   // Si falló...
    {
        glGetShaderInfoLog(shader, 512, NULL, infoLog); // ...obtiene el mensaje de error del compilador GLSL
        std::cerr << "Error compilando shader:\n" << infoLog << std::endl; // ...y lo imprime
    }
    return shader;                                  // Devuelve el ID del shader
}

// ---- Une vertex + fragment shader en un programa ejecutable en la GPU ----
unsigned int createShaderProgram(const char* vsSource, const char* fsSource)
{
    unsigned int vs = compileShader(GL_VERTEX_SHADER, vsSource);     // Compila el vertex shader
    unsigned int fs = compileShader(GL_FRAGMENT_SHADER, fsSource);   // Compila el fragment shader

    unsigned int program = glCreateProgram();       // Crea un programa vacío
    glAttachShader(program, vs);                    // Adjunta el vertex shader
    glAttachShader(program, fs);                    // Adjunta el fragment shader
    glLinkProgram(program);                         // Enlaza: conecta las salidas del vertex con las entradas del fragment

    int success;                                    // Estado del enlazado
    char infoLog[512];                              // Buffer para el mensaje de error
    glGetProgramiv(program, GL_LINK_STATUS, &success); // Consulta si el enlazado funcionó
    if (!success)                                   // Si falló...
    {
        glGetProgramInfoLog(program, 512, NULL, infoLog); // ...obtiene el mensaje
        std::cerr << "Error enlazando programa:\n" << infoLog << std::endl; // ...y lo imprime
    }

    glDeleteShader(vs);                             // Los shaders individuales ya no hacen falta una vez enlazados
    glDeleteShader(fs);                             // Ídem
    return program;                                 // Devuelve el ID del programa
}

// ---- Crea una textura 2D desde un archivo; si no se encuentra, genera un tablero de ajedrez ----
unsigned int loadTexture(const char* path)
{
    unsigned int textureID;                         // ID de la textura
    glGenTextures(1, &textureID);                   // Crea 1 objeto textura en OpenGL
    glBindTexture(GL_TEXTURE_2D, textureID);        // Lo activa como textura 2D actual (lo siguiente se aplica a esta textura)

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);           // Si u sale de [0,1], la textura se repite en horizontal
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);           // Ídem en vertical (v)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); // Al verse pequeña: mezcla mipmaps y texels vecinos (suave)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);       // Al verse grande: interpolación lineal entre texels

    stbi_set_flip_vertically_on_load(true);         // Las imágenes se guardan de arriba a abajo; OpenGL espera abajo a arriba -> se voltea
    int width, height, channels;                    // Ancho, alto y número de canales de la imagen cargada
    unsigned char* data = stbi_load(path, &width, &height, &channels, 0); // Lee el archivo; devuelve NULL si falla

    if (data)                                       // Si la imagen se cargó bien
    {
        GLenum format = (channels == 4) ? GL_RGBA : GL_RGB; // 4 canales = con transparencia (PNG); 3 = RGB (JPG)
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);      // Filas sin relleno de alineación (evita imágenes torcidas con anchos no múltiplos de 4)
        glTexImage2D(GL_TEXTURE_2D,                 // Destino: textura 2D
            0,                             // Nivel mipmap 0 (imagen original)
            format,                        // Formato interno en la GPU
            width, height,                 // Dimensiones
            0,                             // Borde (siempre 0)
            format,                        // Formato de los datos de origen
            GL_UNSIGNED_BYTE,              // Cada componente es un byte
            data);                         // Los píxeles
        glGenerateMipmap(GL_TEXTURE_2D);            // Genera automáticamente las versiones reducidas de la textura
        stbi_image_free(data);                      // Libera la memoria de la CPU (la imagen ya está en la GPU)
    }
    else                                            // Si no se pudo cargar el archivo
    {
        std::cerr << "No se encontro '" << path << "'. Usando textura de tablero." << std::endl; // Avisa
        const int size = 64;                        // Tablero de 64x64 píxeles
        unsigned char checker[size * size * 3];     // 3 bytes (RGB) por píxel
        for (int y = 0; y < size; y++)              // Recorre filas
        {
            for (int x = 0; x < size; x++)          // Recorre columnas
            {
                bool white = ((x / 8) + (y / 8)) % 2 == 0;  // Cuadros de 8x8 píxeles alternando blanco/color
                int i = (y * size + x) * 3;                 // Posición del píxel en el arreglo
                checker[i + 0] = white ? 255 : 200;         // Rojo
                checker[i + 1] = white ? 255 : 60;          // Verde
                checker[i + 2] = white ? 255 : 60;          // Azul
            }
        }
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);      // Sin relleno de alineación
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, size, size, 0, GL_RGB, GL_UNSIGNED_BYTE, checker); // Sube el tablero a la GPU
        glGenerateMipmap(GL_TEXTURE_2D);            // Genera los mipmaps
    }
    return textureID;                               // Devuelve el ID de la textura
}
	