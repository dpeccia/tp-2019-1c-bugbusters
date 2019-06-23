#include "kernel.h"

int main(void) {
	inicializarVariables();

	log_info(logger_KERNEL, "----------------INICIO DE KERNEL--------------");

	//Hilos
	pthread_create(&hiloConectarAMemoria, NULL, (void*)conectarAMemoria, NULL);
	pthread_create(&hiloPlanificarNew, NULL, (void*)planificarNewAReady, NULL);
	pthread_create(&hiloPlanificarExec, NULL, (void*)planificarReadyAExec, NULL);
	pthread_create(&hiloMetricas, NULL, (void*)loguearMetricas, NULL);
	pthread_create(&hiloDescribe, NULL, (void*)hacerDescribe, NULL);
	pthread_create(&hiloCambioEnConfig, NULL, (void*)escucharCambiosEnConfig, NULL);
	pthread_create(&hiloLeerDeConsola, NULL, (void*)leerDeConsola, NULL);

	pthread_join(hiloConectarAMemoria, NULL);
	pthread_join(hiloPlanificarNew, NULL);
	pthread_join(hiloPlanificarExec, NULL);
	pthread_join(hiloMetricas, NULL);
	pthread_join(hiloDescribe, NULL);
	pthread_join(hiloLeerDeConsola, NULL);
	pthread_join(hiloCambioEnConfig, NULL);

	liberarMemoria();

	return EXIT_SUCCESS;
}

// hilo de gossiping
// tener en cuenta caida de memoria y sacarla de las listas

/* inicializarVariables()
 * Parametros:
 * 	-> void
 * Descripcion: inicializa todas las variables que se van a usar.
 * Return:
 * 	-> :: void  */
void inicializarVariables() {
	logger_KERNEL = log_create("kernel.log", "Kernel", 1, LOG_LEVEL_DEBUG);
	logger_METRICAS_KERNEL = log_create("metricas.log", "MetricasKernel", 0, LOG_LEVEL_DEBUG);

	// Configs
	config = leer_config("/home/utnso/tp-2019-1c-bugbusters/kernel/kernel.config");
	int multiprocesamiento = config_get_int_value(config, "MULTIPROCESAMIENTO");
	quantum = config_get_int_value(config, "QUANTUM");
	sleepEjecucion = config_get_int_value(config, "SLEEP_EJECUCION");
	metadataRefresh = config_get_int_value(config, "METADATA_REFRESH");
	numeroSeedRandom = config_get_int_value(config, "DAIU_NUMBER");
	srand(numeroSeedRandom);

	// Semáforos
	sem_init(&semRequestNew, 0, 0);
	sem_init(&semRequestReady, 0, 0);
	sem_init(&semMultiprocesamiento, 0, multiprocesamiento);
	pthread_mutex_init(&semMColaNew, NULL);
	pthread_mutex_init(&semMColaReady, NULL);
	pthread_mutex_init(&semMMetricas, NULL);
	pthread_mutex_init(&semMTablasSC, NULL);
	pthread_mutex_init(&semMTablasSHC, NULL);
	pthread_mutex_init(&semMTablasEC, NULL);
	pthread_mutex_init(&semMQuantum, NULL);
	pthread_mutex_init(&semMSleepEjecucion, NULL);
	pthread_mutex_init(&semMMetadataRefresh, NULL);
	pthread_mutex_init(&semMMemoriaSC, NULL);
	pthread_mutex_init(&semMMemoriasSHC, NULL);
	pthread_mutex_init(&semMMemoriasEC, NULL);
	pthread_mutex_init(&semMMemorias, NULL);

	// Colas de planificacion
	new = queue_create();
	ready = queue_create();
	// Memorias
	memoriasEc = list_create();
	memoriasShc = list_create();
	memorias = list_create();
	memoriaSc = NULL;
	// Tablas
	tablasSC = list_create();
	tablasSHC = list_create();
	tablasEC = list_create();
	// Metricas
	cargaMemoria = list_create();
}

/* liberarMemoria()
 * Parametros:
 * 	-> void
 * Descripcion: libera los recursos.
 * Return:
 * 	-> void  */
void liberarMemoria(void) {
	liberar_conexion(conexionMemoria);

	config_destroy(config);

	pthread_mutex_destroy(&semMColaNew);
	pthread_mutex_destroy(&semMColaReady);
	pthread_mutex_destroy(&semMMetricas);
	pthread_mutex_destroy(&semMTablasSC);
	pthread_mutex_destroy(&semMTablasSHC);
	pthread_mutex_destroy(&semMTablasEC);
	pthread_mutex_destroy(&semMQuantum);
	pthread_mutex_destroy(&semMSleepEjecucion);
	pthread_mutex_destroy(&semMMetadataRefresh);
	pthread_mutex_destroy(&semMMemoriaSC);
	pthread_mutex_destroy(&semMMemoriasSHC);
	pthread_mutex_destroy(&semMMemoriasEC);
	pthread_mutex_destroy(&semMMemorias);

	sem_destroy(&semRequestNew);
	sem_destroy(&semRequestReady);
	sem_destroy(&semMultiprocesamiento);

	queue_destroy(new);
	queue_destroy(ready);

	list_destroy_and_destroy_elements(memorias, (void*)liberarConfigMemoria);
	list_destroy(memoriasShc);
	list_destroy(memoriasEc);
	list_destroy_and_destroy_elements(tablasSC, (void*)liberarTabla);
	list_destroy_and_destroy_elements(tablasSHC, (void*)liberarTabla);
	list_destroy_and_destroy_elements(tablasEC, (void*)liberarTabla);
	list_destroy_and_destroy_elements(cargaMemoria, (void*)liberarEstadisticaMemoria);

	inotify_rm_watch(file_descriptor, watch_descriptor);
	close(file_descriptor);

	log_info(logger_KERNEL, "Estoy liberando toda la memoria, chau");
	log_destroy(logger_KERNEL);
	log_destroy(logger_METRICAS_KERNEL);
}

/* conectarAMemoria()
 * Parametros:
 * 	-> void
 * Descripcion: conecta kernel con memoria.
 * Return:
 * 	-> :: void  */
void conectarAMemoria(void) {
	t_handshake_memoria* handshake;
	conexionMemoria = crearConexion(config_get_string_value(config, "IP_MEMORIA"), config_get_string_value(config, "PUERTO_MEMORIA"));
	handshake = recibirHandshakeMemoria(conexionMemoria);
	procesarHandshake(handshake);
	liberarHandshakeMemoria(handshake);
}

/* procesarHandshake()
 * Parametros:
 * 	-> t_handshake_memoria* :: handshakeRecibido
 * Descripcion: guarda las memorias levantadas en una lista.
 * Return:
 * 	-> :: void  */
void procesarHandshake(t_handshake_memoria* handshakeRecibido) {
	size_t i = 0;
	char** ips = string_split(handshakeRecibido->ips, ",");
	char** puertos = string_split(handshakeRecibido->puertos, ",");
	char** numeros = string_split(handshakeRecibido->numeros, ",");
	for(i = 0; ips[i] != NULL; i++)
	{
		config_memoria* memoriaNueva = (config_memoria*) malloc(sizeof(config_memoria));
		memoriaNueva->ip = strdup(ips[i]);// config_get_string_value(config, "IP_MEMORIA");
		memoriaNueva->puerto = strdup(puertos[i]);// config_get_string_value(config, "PUERTO_MEMORIA");
		memoriaNueva->numero = strdup(numeros[i]);
		log_info(logger_KERNEL, "estoy agregando el ip: %s puerto: %s numero: %s", memoriaNueva->ip, memoriaNueva->puerto, memoriaNueva->numero);
		pthread_mutex_lock(&semMMemorias);
		list_add(memorias, memoriaNueva);
		pthread_mutex_unlock(&semMMemorias);
		memoriaNueva = NULL;
	}
	liberarArrayDeChar(ips);
	liberarArrayDeChar(puertos);
	liberarArrayDeChar(numeros);
}

/* leerDeConsola()
 * Parametros:
 * 	-> void
 * Descripcion: es un hilo que lee input de consola y agrega los requests a cola de new.
 * Return:
 * 	-> :: void  */
void leerDeConsola(void){
	char* mensaje;
	while (1) {
		mensaje = readline(">");
		if (!strcmp(mensaje, "\0")) {
			pthread_cancel(hiloConectarAMemoria);
			pthread_cancel(hiloPlanificarNew);
			pthread_cancel(hiloPlanificarExec);
			pthread_cancel(hiloMetricas);
			pthread_cancel(hiloDescribe);
			pthread_cancel(hiloCambioEnConfig);
			free(mensaje);
			break;
		}
		planificarRequest(mensaje);
	}
}

/* planificarRequest()
 * Parametros:
 * 	-> char* :: request
 * Descripcion: recibe una request, si es ADD o METRICS lo ejecuta directamente, sino lo agrega a la cola de new.
 * Return:
 * 	-> :: void  */
void planificarRequest(char* request) {
	char** requestSeparada = string_n_split(request, 2, " ");
	cod_request codigo = obtenerCodigoPalabraReservada(requestSeparada[0], KERNEL);
	if (codigo == ADD || codigo == METRICS) {
		// https://github.com/sisoputnfrba/foro/issues/1382
		pthread_t hiloRequest;
		pthread_attr_t attr;
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		int threadProcesar = pthread_create(&hiloRequest, &attr, (void*)procesarRequestSinPlanificar, request);
		if(threadProcesar == 0){
			pthread_attr_destroy(&attr);
		} else {
			log_error(logger_KERNEL, "Hubo un error al crear el hilo que ejecuta la request");
		}
	} else {
		pthread_mutex_lock(&semMColaNew);
		//agregar request a la cola de new
		queue_push(new, request);
		pthread_mutex_unlock(&semMColaNew);
		sem_post(&semRequestNew);
	}
	liberarArrayDeChar(requestSeparada);
}

void procesarRequestSinPlanificar(char* request) {
	char** requestSeparada = string_n_split(request, 2, " ");
	cod_request codigo = obtenerCodigoPalabraReservada(requestSeparada[0], KERNEL);
	request_procesada* requestArmada = (request_procesada*) malloc(sizeof(request_procesada));
	requestArmada->codigo = codigo;
	requestArmada->request = strdup(request);
	manejarRequest(requestArmada);
	liberarRequestProcesada(requestArmada);
	free(request);
	liberarArrayDeChar(requestSeparada);
}

/* escucharCambiosEnConfig()
 * Parametros:
 * 	-> void
 * Descripcion: es un hilo que escucha cambios en el config y actualiza las variables correspodientes.
 * Return:
 * 	-> :: void  */
void escucharCambiosEnConfig(void) {
	char buffer[BUF_LEN];
	file_descriptor = inotify_init();
	if (file_descriptor < 0) {
		log_error(logger_KERNEL, "Inotify no se pudo inicializar correctamente");
	}

	watch_descriptor = inotify_add_watch(file_descriptor, "/home/utnso/tp-2019-1c-bugbusters/kernel/kernel.config", IN_MODIFY);
	while(1) {
		log_info(logger_KERNEL, "Cambió el archivo de config");
		int length = read(file_descriptor, buffer, BUF_LEN);
		if (length < 0) {
			log_error(logger_KERNEL, "Error en inotify");
		} else {
			pthread_mutex_lock(&semMQuantum);
			quantum = config_get_int_value(config, "QUANTUM");
			pthread_mutex_unlock(&semMQuantum);
			pthread_mutex_lock(&semMSleepEjecucion);
			sleepEjecucion = config_get_int_value(config, "SLEEP_EJECUCION");
			pthread_mutex_unlock(&semMSleepEjecucion);
			pthread_mutex_lock(&semMMetadataRefresh);
			metadataRefresh = config_get_int_value(config, "METADATA_REFRESH");
			pthread_mutex_unlock(&semMMetadataRefresh);
		}

	}
}

/* hacerDescribe()
 * Parametros:
 * 	-> void
 * Descripcion: cada x segundos hace describe global.
 * Return:
 * 	-> :: void  */
void hacerDescribe(void) {
	while(1) {
		break;
		// agregar a new? o ejecutarlo directamente? se ejecuta directamente
		usleep(metadataRefresh * 1000);
	}
}

/* loguearMetricas()
 * Parametros:
 * 	-> void
 * Descripcion: cada 30 segundos loguea data.
 * Return:
 * 	-> :: void  */
void loguearMetricas(void) {
	while(1) {
		informarMetricas(FALSE);
		// limpio variables para empezar a contar de nuevo
		pthread_mutex_lock(&semMMetricas);
		tiempoSelect = 0.0;
		tiempoInsert = 0.0;
		cantidadSelect = 0;
		cantidadInsert = 0;
		cantidadTotalRequest = 0;
		if (list_size(cargaMemoria) > 0) {
			list_clean_and_destroy_elements(cargaMemoria, (void*)liberarEstadisticaMemoria);
		}
		pthread_mutex_unlock(&semMMetricas);
		sleep(30);
	}
}

/* informarMetricas()
 * Parametros:
 * 	-> int :: mostrarPorConsola
 * Descripcion: si parametro es true muestra metricas actuales por consola,
 * sino en archivo de log de métricas.
 * Return:
 * 	-> :: void  */
void informarMetricas(int mostrarPorConsola) {
	// si es vacio no mostrar basura
	pthread_mutex_lock(&semMMetricas);
	double readLatency = tiempoSelect/cantidadSelect;
	double writeLatency = tiempoInsert/cantidadInsert;
	void mostrarCargaMemoria(estadisticaMemoria* estadisticaAMostrar) {
		double estadistica = (double)estadisticaAMostrar->cantidadSelectInsert / cantidadTotalRequest;
		if (mostrarPorConsola == TRUE) {
			log_info(logger_KERNEL, "La cantidad de Select - Insert respecto del resto de las operaciones de la memoria %s es %f",
					estadisticaAMostrar->numeroMemoria, estadistica);
		} else {
			log_info(logger_METRICAS_KERNEL, "La cantidad de Select - Insert respecto del resto de las operaciones de la memoria %s es %f",
					estadisticaAMostrar->numeroMemoria, estadistica);
		}
	}
	if (mostrarPorConsola == TRUE) {
		log_info(logger_KERNEL, "Read Latency de SC: %f", readLatency);
		log_info(logger_KERNEL, "Write Latency de SC: %f", writeLatency);
		if (list_size(cargaMemoria) > 0) {
			log_info(logger_KERNEL, "El memory load es:");
			list_iterate(cargaMemoria, (void*) mostrarCargaMemoria);
		}
		log_info(logger_KERNEL, "Cantidad de Reads de SC: %d", cantidadSelect);
		log_info(logger_KERNEL, "Cantidad de Writes de SC: %d", cantidadInsert);
	} else {
		log_info(logger_METRICAS_KERNEL, "Read Latency de SC: %f", readLatency);
		log_info(logger_METRICAS_KERNEL, "Write Latency de SC: %f", writeLatency);
		if (list_size(cargaMemoria) > 0) {
			log_info(logger_METRICAS_KERNEL, "El memory load es:");
			list_iterate(cargaMemoria, (void*) mostrarCargaMemoria);
		}
		log_info(logger_METRICAS_KERNEL, "Cantidad de Reads de SC: %d", cantidadSelect);
		log_info(logger_METRICAS_KERNEL, "Cantidad de Writes de SC: %d", cantidadInsert);
	}
	pthread_mutex_unlock(&semMMetricas);
}

/* liberarEstadisticaMemoria()
 * Parametros:
 * 	-> estadisticaMemoria* :: memoria
 * Descripcion: recibe una estadisticaMemoria y la libera.
 * Return:
 * 	-> :: void  */
void liberarEstadisticaMemoria(estadisticaMemoria* memoria) {
	free(memoria->numeroMemoria);
	free(memoria);
}

/* liberarConfigMemoria()
 * Parametros:
 * 	-> config_memoria* :: configALiberar
 * Descripcion: recibe una configMemoria y la libera.
 * Return:
 * 	-> :: void  */
void liberarConfigMemoria(config_memoria* configALiberar) {
	if (configALiberar != NULL) {
		free(configALiberar->ip);
		free(configALiberar->numero);
		free(configALiberar->puerto);
		configALiberar->ip = NULL;
		configALiberar->puerto = NULL;
		configALiberar->numero = NULL;
	}
	free(configALiberar);
	configALiberar = NULL;
}

/* aumentarContadores()
 * Parametros:
 * 	-> consistencia :: consistenciaRequest
 * 	-> char* :: numeroMemoria
 * 	-> cod_request* :: codigo
 * 	-> int :: cantidadTiempo
 * Descripcion: aumenta las variables para las métricas.
 * Return:
 * 	-> :: void  */
void aumentarContadores(char* numeroMemoria, cod_request codigo, double cantidadTiempo) {
	estadisticaMemoria* memoriaCorrespondiente;
	// si es un describe global va a entrar al "NINGUNA"
	// cargo en el criterio segun el request o segun la memoria?
	// o sea si tengo una memory que es SC y EC y le hago dos select que son de una tabla
	// EC, en la cantidad total de request en mi lista de SC de esa memoria es de 2 o 0? porque
	// no se hizo nada SC pero si se hizo a esa memoria PREGUNTAR
	int encontrarTabla(estadisticaMemoria* memoria) {
		return string_equals_ignore_case(memoria->numeroMemoria, numeroMemoria);
	}
	int cantidadASumarSelectInsert = codigo == SELECT || codigo == INSERT;
	pthread_mutex_lock(&semMMetricas);

	memoriaCorrespondiente = list_find(cargaMemoria, (void*)encontrarTabla);
	if (memoriaCorrespondiente == NULL) {
		memoriaCorrespondiente = NULL;
		memoriaCorrespondiente = (estadisticaMemoria*) malloc(sizeof(estadisticaMemoria));
		memoriaCorrespondiente->cantidadSelectInsert = cantidadASumarSelectInsert;
		memoriaCorrespondiente->numeroMemoria = strdup(numeroMemoria);
		list_add(cargaMemoria, memoriaCorrespondiente);
	} else {
		// se actualiza solo? porque no es un puntero un int, ver si hay que hacer otra cosa
		memoriaCorrespondiente->cantidadSelectInsert += cantidadASumarSelectInsert;
	}

	cantidadTotalRequest++;

	if (codigo == SELECT) {
		tiempoSelect += cantidadTiempo;
		cantidadSelect++;
	} else if (codigo == INSERT) {
		tiempoInsert += cantidadTiempo;
		cantidadInsert++;
	}
	pthread_mutex_unlock(&semMMetricas);
}

/*
 Planificación
*/

/* planificarNewAReady()
 * Parametros:
 * 	-> void
 * Descripcion: hilo que, luego de que un request es agregado a la cola de new, lo toma
 * y reserva recursos si y solo si es válido - en el caso de RUN solo valida si el RUN es válido,
 * no las requests dentro del archivo.
 * Return:
 * 	-> :: void  */
void planificarNewAReady(void) {
	while(1) {
		sem_wait(&semRequestNew);
		pthread_mutex_lock(&semMColaNew);
		char* request = strdup((char*) queue_peek(new));
		free((char*) queue_pop(new));
		pthread_mutex_unlock(&semMColaNew);
		if(validarRequest(request) == TRUE) {
			//cuando es run, en vez de pushear request, se pushea array de requests, antes se llama a reservar recursos que hace eso
			reservarRecursos(request);
		} else {
			free(request);
			//error
		}
	}
}

/* planificarReadyAExec()
 * Parametros:
 * 	-> void
 * Descripcion: hilo que, luego de que un request es agregado a la cola de ready, lo toma
 * y si no se superó el nivel de multiprocesamiento, lo ejecuta y lo agrega a la cola de exec.
 * Return:
 * 	-> :: void  */
void planificarReadyAExec(void) {
	pthread_t hiloRequest;
	pthread_attr_t attr;
	request_procesada* request;
	int threadProcesar;
	int d;
	int a;
	while(1) {
		sem_wait(&semRequestReady);
		sem_getvalue(&semMultiprocesamiento, &a);
		sem_wait(&semMultiprocesamiento);
		sem_getvalue(&semMultiprocesamiento, &d);
		request = (request_procesada*) malloc(sizeof(request_procesada));
		pthread_mutex_lock(&semMColaReady);
		request->codigo = ((request_procesada*) queue_peek(ready))->codigo;
		if(request->codigo == RUN) {
			request->request = ((request_procesada*)queue_peek(ready))->request;
		} else {
			request->request = strdup((char*)((request_procesada*)queue_peek(ready))->request);
			free((char*)((request_procesada*)queue_peek(ready))->request);
		}
		free(queue_pop(ready));
		pthread_mutex_unlock(&semMColaReady);
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		threadProcesar = pthread_create(&hiloRequest, &attr, (void*)procesarRequest, request);
		if(threadProcesar == 0){
			pthread_attr_destroy(&attr);
		} else {
			log_error(logger_KERNEL, "Hubo un error al crear el hilo que ejecuta la request");
		}
	}
}

/* reservarRecursos()
 * Parametros:
 * 	-> char* :: mensaje
 * Descripcion: toma request y reserva recursos. Si es RUN lee el archivo y va agregando a una cola,
 * despues esta es agregada a la cola de ready,
 * sino, simplemente lo agrega a la cola de ready.
 * Return:
 * 	-> :: void  */
void reservarRecursos(char* mensaje) {
	char** request = string_n_split(mensaje, 2, " ");
	cod_request codigo = obtenerCodigoPalabraReservada(request[0], KERNEL);
	request_procesada* _request = (request_procesada*) malloc(sizeof(request_procesada));
	if (codigo == RUN) {
		//desarmo archivo y lo pongo en una cola
		FILE *archivoLql;
		char** parametros;
		parametros = separarRequest(mensaje);
		archivoLql = fopen(parametros[1], "r");
		if (archivoLql == NULL) {
			log_error(logger_KERNEL, "No existe un archivo en esa ruta");
			liberarArrayDeChar(parametros);
			liberarArrayDeChar(request);
			free(mensaje);
			free(_request);
			return;
		} else {
			_request->request = queue_create();
			char letra;
			char* request;
			char** requestDividida;
			int i = 1;
			size_t posicion = 0;
			request = (char*) malloc(sizeof(char));
		    *request = '\0';
			while((letra = fgetc(archivoLql)) != EOF) {
				if (letra != '\n') {
					// concateno
					i++;
					request = (char*) realloc(request, i * sizeof(char));
					posicion = strlen(request);
					request[posicion] = letra;
					request[posicion + 1] = '\0';
				} else {
					request_procesada* otraRequest = (request_procesada*) malloc(sizeof(request_procesada));
					requestDividida = string_n_split(request, 2, " ");
					cod_request _codigo = obtenerCodigoPalabraReservada(requestDividida[0], KERNEL);
					otraRequest->codigo = _codigo;
					otraRequest->request = strdup(request);
					queue_push(_request->request, otraRequest);
					liberarArrayDeChar(requestDividida);
					i = 1;
					otraRequest = NULL;
					free(request);
					request = NULL;
					request = (char*) malloc(sizeof(char));
				    *request = '\0';
				}
			}
			if (i != 1) { //hack horrible para que funcione cuando los lql tienen salto de linea al final y cuando no tmb
				//esto esta para la ultima linea, porque como no tiene salto de linea no entra al else y no se guarda sino
				request_procesada* otraRequest = (request_procesada*) malloc(sizeof(request_procesada));
				requestDividida = string_n_split(request, 2, " ");
				cod_request _codigo = obtenerCodigoPalabraReservada(requestDividida[0], KERNEL);
				otraRequest->codigo = _codigo;
				otraRequest->request = strdup(request);
				queue_push(_request->request, otraRequest);
				liberarArrayDeChar(requestDividida);
			}
			free(request);
			request = NULL;
			liberarArrayDeChar(parametros);
			fclose(archivoLql);
		}
		_request->codigo = RUN;
		pthread_mutex_lock(&semMColaReady);
		queue_push(ready, _request);
		pthread_mutex_unlock(&semMColaReady);
	} else {
		_request->request = strdup(mensaje);
		_request->codigo = codigo;
		pthread_mutex_lock(&semMColaReady);
		queue_push(ready, _request);
		pthread_mutex_unlock(&semMColaReady);
	}
	free(mensaje);
	liberarArrayDeChar(request);
	sem_post(&semRequestReady);
}

/* liberarRequestProcesada()
 * Parametros:
 * 	-> request_procesada* :: request
 * Descripcion: libera los recursos de la request.
 * Return:
 * 	-> void  */
void liberarRequestProcesada(request_procesada* request) {
	if(request->codigo != RUN) {
       free(request->request);
       request->request = NULL;
	}
	// en el caso del run ya se libera la cola en su funcion
	// solo hacer este free si request fue a exit
	free(request);
	request = NULL;
	log_info(logger_KERNEL, "libero request");
}

/* liberarColaRequest()
 * Parametros:
 *     -> request_procesada* :: requestCola
 * Descripcion: recibe una request que se encuentra dentro de una cola y la libera.
 * Return:
 *     -> void  */
void liberarColaRequest(request_procesada* requestCola) {
	//printf("Voy a liberar: %s    ", (char*) requestCola->request);
    free((char*) requestCola->request);
    requestCola->request = NULL;
    free(requestCola);
    requestCola = NULL;
}

/* procesarRequest()
 * Parametros:
 * 	-> request_procesada* :: request
 * Descripcion: hilo que se crea dinámicamente, desde planificarReadyAExec, maneja la request y libera sus recursos.
 * Return:
 * 	-> :: void  */
void procesarRequest(request_procesada* request) {
	manejarRequest(request);
	liberarRequestProcesada(request);
	request = NULL;
	sem_post(&semMultiprocesamiento);
}

/* validarRequest()
 * Parametros:
 * 	-> char* :: mensaje
 * Descripcion: toma un request y se fija si es válido.
 * Return:
 * 	-> requestEsValida :: bool  */
int validarRequest(char* mensaje) {
	int codValidacion = validarMensaje(mensaje, KERNEL, logger_KERNEL);
	switch(codValidacion) {
		case EXIT_SUCCESS:
			return TRUE;
			break;
		case EXIT_FAILURE:
		case NUESTRO_ERROR:
			return FALSE;
			//informar error
			break;
		default:
			return FALSE;
			break;
	}
}

/*
 * Se ocupa de delegar la request
*/

/* manejarRequest()
 * Parametros:
 * 	-> request_procesada* :: request
 * Descripcion: toma un request y se fija a quién delegarselo, toma la respuesta y la devuelve.
 * Return:
 * 	-> respuesta :: t_paquete*  */
int manejarRequest(request_procesada* request) {
	int respuesta = 0;
	//devolver la respuesta para usarla en el run
	switch(request->codigo) {
		case SELECT:
		case INSERT:
		case CREATE:
		case DESCRIBE:
		case DROP:
			respuesta = enviarMensajeAMemoria(request->codigo, (char*) request->request);
			break;
		case JOURNAL:
			procesarJournal(FALSE);
			// solo a memorias que tengan un criterio
			break;
		case ADD:
			respuesta = procesarAdd((char*) request->request);
			break;
		case RUN:
			procesarRun((t_queue*) request->request);
			break;
		case METRICS:
			informarMetricas(TRUE);
			break;
		default:
			// aca puede entrar solo si viene de run, porque sino antes siempre fue validada
			// la request. en tal caso se devuelve error para cortar ejecucion
			break;
	}
	return respuesta;
}

/* actualizarTablas()
 * Parametros:
 * 	-> char* :: respuesta
 * Descripcion: toma una rta del DESCRIBE y actualiza data de las tablas.
 * Return:
 * 	-> void */
void actualizarTablas(char* respuesta) {
	// formato de rta: tabla consistencia particiones tiempoDeCompactacion;
	// separo por ; y despues itero sobre eso
	char** respuestaParseada = string_split(respuesta, ";");
	int esDescribeGlobal = longitudDeArrayDeStrings(respuestaParseada) != 1;
	// solo limpiar cuando es global!!!
	if (esDescribeGlobal == TRUE) {
		pthread_mutex_lock(&semMTablasSC);
		list_clean_and_destroy_elements(tablasSC, (void*)liberarTabla);
		pthread_mutex_unlock(&semMTablasSC);
		pthread_mutex_lock(&semMTablasSHC);
		list_clean_and_destroy_elements(tablasSHC, (void*)liberarTabla);
		pthread_mutex_unlock(&semMTablasSHC);
		pthread_mutex_lock(&semMTablasEC);
		list_clean_and_destroy_elements(tablasEC, (void*)liberarTabla);
		pthread_mutex_unlock(&semMTablasEC);
		string_iterate_lines(respuestaParseada, (void*)agregarTablaACriterio);
	} else {
		agregarTablaACriterio(respuestaParseada[0]);
	}
	liberarArrayDeChar(respuestaParseada);
}

/* recorrerTabla()
 * Parametros:
 * 	-> char* :: tabla
 * Descripcion: toma una tabla, se fija que consistencia tiene y la guarda donde corresponde si no existe.
 * Si ya existe no hace nada y si la tiene que actualizar lo hace.
 * Return:
 * 	-> void */
void agregarTablaACriterio(char* tabla) {
	// busco la tabla y actualizo si cambio la consistencia y si no la creo
	char** tablaParseada = string_split(tabla, " ");
	char* nombreTabla = strdup(tablaParseada[0]);
	char* consistenciaTabla = tablaParseada[1];
	consistencia tipoConsistencia = obtenerEnumConsistencia(consistenciaTabla);
	int hayQueAgregar = FALSE;
	int esTabla(char* tablaActual) {
		return string_equals_ignore_case(nombreTabla, tablaActual);
	}
	pthread_mutex_lock(&semMTablasSC);
	pthread_mutex_lock(&semMTablasSHC);
	pthread_mutex_lock(&semMTablasEC);
	if (list_find(tablasSC, (void*)esTabla) != NULL) {
		if (tipoConsistencia != SC) {
			hayQueAgregar = TRUE;
			list_remove_and_destroy_by_condition(tablasSC, (void*)esTabla, (void*)liberarTabla);
		} else {
			free(nombreTabla);
		}
		pthread_mutex_unlock(&semMTablasSC);
		pthread_mutex_unlock(&semMTablasSHC);
		pthread_mutex_unlock(&semMTablasEC);
	} else if (list_find(tablasSHC, (void*)esTabla) != NULL) {
		if (tipoConsistencia != SHC) {
			hayQueAgregar = TRUE;
			list_remove_and_destroy_by_condition(tablasSHC, (void*)esTabla, (void*)liberarTabla);
		} else {
			free(nombreTabla);
		}
		pthread_mutex_unlock(&semMTablasSC);
		pthread_mutex_unlock(&semMTablasSHC);
		pthread_mutex_unlock(&semMTablasEC);
	} else if (list_find(tablasEC, (void*)esTabla) != NULL) {
		if (tipoConsistencia != EC) {
			hayQueAgregar = TRUE;
			list_remove_and_destroy_by_condition(tablasEC, (void*)esTabla, (void*)liberarTabla);
		} else {
			free(nombreTabla);
		}
		pthread_mutex_unlock(&semMTablasSC);
		pthread_mutex_unlock(&semMTablasSHC);
		pthread_mutex_unlock(&semMTablasEC);
	} else {
		pthread_mutex_unlock(&semMTablasSC);
		pthread_mutex_unlock(&semMTablasSHC);
		pthread_mutex_unlock(&semMTablasEC);
		hayQueAgregar = TRUE;
	}
	if (hayQueAgregar == TRUE) {
		// tabla no existe en estructura o hay que agregarla en otra
		switch(tipoConsistencia) {
			case SC:
				pthread_mutex_lock(&semMTablasSC);
				list_add(tablasSC, nombreTabla);
				pthread_mutex_unlock(&semMTablasSC);
				log_info(logger_KERNEL, "Agregue la tabla %s al criterio SC", nombreTabla);
				break;
			case SHC:
				pthread_mutex_lock(&semMTablasSHC);
				list_add(tablasSHC, nombreTabla);
				pthread_mutex_unlock(&semMTablasSHC);
				log_info(logger_KERNEL, "Agregue la tabla %s al criterio SHC", nombreTabla);
				break;
			case EC:
				pthread_mutex_lock(&semMTablasEC);
				list_add(tablasEC, nombreTabla);
				pthread_mutex_unlock(&semMTablasEC);
				log_info(logger_KERNEL, "Agregue la tabla %s al criterio EC", nombreTabla);
				break;
			default:
				log_error(logger_KERNEL, "La tabla %s no tiene asociada un criterio válido y no se actualizó en la estructura de datos",
						nombreTabla);
				break;
		}
	}
	liberarArrayDeChar(tablaParseada);
}

/* liberarTabla()
 * Parametros:
 * 	-> char* :: tabla
 * Descripcion: toma una tabla y la libera.
 * Return:
 * 	-> void */
void liberarTabla(char* tabla) {
	free(tabla);
	tabla = NULL;
}

/* obtenerConsistenciaTabla()
 * Parametros:
 * 	-> char* :: tabla
 * Descripcion: toma una tabla y devuelve el criterio correspondiente.
 * Return:
 * 	-> consistenciaCorrespondiente :: consistencia*  */
consistencia obtenerConsistenciaTabla(char* tabla) {
	int esTabla(char* tablaActual) {
		return string_equals_ignore_case(tabla, tablaActual);
	}
	pthread_mutex_lock(&semMTablasSC);
	pthread_mutex_lock(&semMTablasSHC);
	pthread_mutex_lock(&semMTablasEC);
	if (list_find(tablasSC, (void*) esTabla) != NULL) {
		pthread_mutex_unlock(&semMTablasSC);
		pthread_mutex_unlock(&semMTablasSHC);
		pthread_mutex_unlock(&semMTablasEC);
		return SC;
	} else if (list_find(tablasSHC, (void*) esTabla) != NULL) {
		pthread_mutex_unlock(&semMTablasSC);
		pthread_mutex_unlock(&semMTablasSHC);
		pthread_mutex_unlock(&semMTablasEC);
		return SHC;
	} else if(list_find(tablasEC, (void*) esTabla) != NULL) {
		pthread_mutex_unlock(&semMTablasSC);
		pthread_mutex_unlock(&semMTablasSHC);
		pthread_mutex_unlock(&semMTablasEC);
		return EC;
	} else {
		pthread_mutex_unlock(&semMTablasSC);
		pthread_mutex_unlock(&semMTablasSHC);
		pthread_mutex_unlock(&semMTablasEC);
		log_error(logger_KERNEL, "No sé que consistencia tiene la tabla %s", tabla);
		return CONSISTENCIA_INVALIDA;
	}
}

/* encontrarMemoriaSegunConsistencia()
 * Parametros:
 * 	-> char* :: tabla
 * 	-> char* :: key
 * Descripcion: toma una tabla y devuelve la memoria según el criterio.
 * En el caso de SHC necesita la key para calcular a qué memoria redirigir.
 * Return:
 * 	-> memoriaCorrespondiente :: config_memoria*  */
config_memoria* encontrarMemoriaSegunConsistencia(consistencia tipoConsistencia) {
	config_memoria* memoriaCorrespondiente = NULL;
	switch(tipoConsistencia) {
		case SC:
			pthread_mutex_lock(&semMMemoriaSC);
			if (memoriaSc == NULL) {
				log_error(logger_KERNEL, "No se puede resolver el request porque no hay memorias asociadas al criterio SC");
			} else {
				memoriaCorrespondiente = memoriaSc;
			}
			pthread_mutex_unlock(&semMMemoriaSC);
			break;
		case SHC:
			// todo: funcion hash
			break;
		case EC:
			pthread_mutex_lock(&semMMemoriasEC);
			unsigned int indice = obtenerIndiceRandom(list_size(memoriasEc));
			memoriaCorrespondiente = list_get(memoriasEc, indice);
			pthread_mutex_unlock(&semMMemoriasEC);
			break;
		default:
			//error
			log_error(logger_KERNEL, "No se puede resolver el request porque no tengo la metadata de la tabla");
			break;
	}
	return memoriaCorrespondiente;
}

/* obtenerIndiceRandom()
 * Parametros:
 * 	-> void
 * Descripcion: devuelve un numero random entre 0 y el max y
 * usa como seed el DAIU_NUMBER de la config.
 * Return:
 * 	-> numero :: unsigned int  */
unsigned int obtenerIndiceRandom(int maximo) {
	unsigned int numero = 0;
	numero = rand() % maximo;
	return numero;
}

/* encontrarMemoriaPpal()
 * Parametros:
 * 	-> config_memoria* :: memoria
 * Descripcion: devuelve TRUE cuando encuentra la memoria ppal.
 * Return:
 * 	-> int  */
int encontrarMemoriaPpal(config_memoria* memoria) {
	return string_equals_ignore_case(memoria->ip, config_get_string_value(config, "IP_MEMORIA"))
			&& string_equals_ignore_case(memoria->puerto, config_get_string_value(config, "PUERTO_MEMORIA"));
}
/*
 * Funciones que procesan requests
*/

/* enviarMensajeAMemoria()
 * Parametros:
 * 	-> cod_request :: codRequest
 * 	-> char* :: mensaje
 * Descripcion: recibe una request, se la manda a memoria y recibe la respuesta.
 * Return:
 * 	-> paqueteRecibido :: t_paquete*  */
int enviarMensajeAMemoria(cod_request codigo, char* mensaje) {
	clock_t tiempo;
	double tiempoQueTardo = 0.0;
	if (codigo == SELECT || codigo == INSERT) {
		tiempo = clock();
	}
	t_paquete* paqueteRecibido;
	int respuesta;
	consistencia consistenciaTabla = NINGUNA;
	int conexionTemporanea = conexionMemoria;
	char** parametros = separarRequest(mensaje);
	config_memoria* memoriaCorrespondiente;
	if (codigo == DESCRIBE) {
		// https://github.com/sisoputnfrba/foro/issues/1391 chequearlo
		pthread_mutex_lock(&semMMemorias);
		unsigned int indice = obtenerIndiceRandom(list_size(memorias));
		memoriaCorrespondiente = list_get(memorias, indice);
		pthread_mutex_unlock(&semMMemorias);
	} else {
		if (codigo == CREATE) {
			consistenciaTabla = obtenerEnumConsistencia(parametros[2]);
		} else {
			consistenciaTabla = obtenerConsistenciaTabla(parametros[1]);
		}
		// semaforo???
		memoriaCorrespondiente = encontrarMemoriaSegunConsistencia(consistenciaTabla);
		if(memoriaCorrespondiente == NULL) {
			respuesta = ERROR_GENERICO;
			liberarArrayDeChar(parametros);
			return respuesta;
		} else {
			if (!string_equals_ignore_case(memoriaCorrespondiente->ip, config_get_string_value(config, "IP_MEMORIA"))
				&& !string_equals_ignore_case(memoriaCorrespondiente->puerto, config_get_string_value(config, "PUERTO_MEMORIA"))) {
				conexionTemporanea = crearConexion(memoriaCorrespondiente->ip, memoriaCorrespondiente->puerto);
			}
		}
	}
	enviar(consistenciaTabla, mensaje, conexionTemporanea);
	paqueteRecibido = recibir(conexionTemporanea);
	respuesta = paqueteRecibido->palabraReservada;
	if (respuesta == SUCCESS) {
		if (codigo == DESCRIBE) {
			actualizarTablas(paqueteRecibido->request);
		}
		if(codigo == SELECT) {
			log_info(logger_KERNEL, "La respuesta del request %s es %s", mensaje, paqueteRecibido->request);
		} else {
			log_info(logger_KERNEL, "El request %s se realizó con éxito", mensaje);
		}
	} else if (respuesta == MEMORIA_FULL) {
		enviar(NINGUNA, "JOURNAL", conexionTemporanea);
		paqueteRecibido = recibir(conexionTemporanea);
		respuesta = paqueteRecibido->palabraReservada;
		if (respuesta == SUCCESS) {
			enviar(consistenciaTabla, mensaje, conexionTemporanea);
			paqueteRecibido = recibir(conexionTemporanea);
			respuesta = paqueteRecibido->palabraReservada;
			if (respuesta == SUCCESS) {
				if(codigo == SELECT) {
					log_info(logger_KERNEL, "La respuesta del request %s es %s", mensaje, paqueteRecibido->request);
				} else {
					log_info(logger_KERNEL, "El request %s se realizó con éxito", mensaje);
				}
			} else {
				log_error(logger_KERNEL, "El request %s no es válido y me llegó como rta %s", mensaje, paqueteRecibido->request);
			}
		} else {
			log_error(logger_KERNEL, "El request %s no es válido y me llegó como rta %s", mensaje, paqueteRecibido->request);
		}
	}
	else {
		log_error(logger_KERNEL, "El request %s no es válido y me llegó como rta %s", mensaje, paqueteRecibido->request);
	}
	if (conexionTemporanea != conexionMemoria) {
		liberar_conexion(conexionTemporanea);
	}
	eliminar_paquete(paqueteRecibido);
	liberarArrayDeChar(parametros);
	if (codigo == SELECT || codigo == INSERT) {
		tiempo = clock() - tiempo;
		tiempoQueTardo = ((double)tiempo)/CLOCKS_PER_SEC;
	}
	aumentarContadores(memoriaCorrespondiente->numero, codigo, tiempoQueTardo);
	return respuesta;
}

/* procesarJournal()
 * Parametros:
 * 	-> soloASHC :: int
 * Descripcion: manda journal a todas las memorias que estén asociadas a un criterio.
 * En el caso de que una memoria es agregada al criterio SHC, se manda
 * un JOURNAL solo a esas memorias.
 * Return:
 * 	-> void */
void procesarJournal(int soloASHC) {
	// sumar a metrica
	// ahora recorro la lista filtrada y creo las conexiones para mandar journal
	// si la memoria es la ppal que ya estoy conectada, no me tengo que conectar
	void enviarJournal(config_memoria* memoriaAConectarse) {
		int conexionTemporanea = conexionMemoria;
		if (!string_equals_ignore_case(memoriaAConectarse->ip, config_get_string_value(config, "IP_MEMORIA"))
			&& !string_equals_ignore_case(memoriaAConectarse->puerto, config_get_string_value(config, "PUERTO_MEMORIA"))) {
			conexionTemporanea = crearConexion(memoriaAConectarse->ip, memoriaAConectarse->puerto);
		}
		enviar(NINGUNA, "JOURNAL", conexionTemporanea);
		log_info(logger_KERNEL, "Se envió el JOURNAL a la memoria con numero %s", memoriaAConectarse->numero);
		/*t_paquete* paqueteRecibido = recibir(conexionTemporanea);
		int respuesta = paqueteRecibido->palabraReservada;
		if (respuesta == SUCCESS) {
			log_info(logger_KERNEL, "La respuesta del request %s es %s \n", "JOURNAL", paqueteRecibido->request);
		} else {
			log_error(logger_KERNEL, "El request %s no es válido", "JOURNAL");
		}*/
		if (conexionTemporanea != conexionMemoria) {
			liberar_conexion(conexionTemporanea);
		}
		// eliminar_paquete(paqueteRecibido);
	}
	if(soloASHC == TRUE) {
		pthread_mutex_lock(&semMMemoriasSHC);
		list_iterate(memoriasShc, (void*)enviarJournal);
		pthread_mutex_unlock(&semMMemoriasSHC);
	} else {
		t_list* memoriasSinRepetir = list_create();
		void agregarMemoriaSinRepetir(config_memoria* memoria) {
			int existeUnaIgual(config_memoria* memoriaAgregada) {
				return string_equals_ignore_case(memoriaAgregada->ip, memoria->ip) &&
						string_equals_ignore_case(memoriaAgregada->puerto, memoria->puerto);
			}
			if (!list_any_satisfy(memoriasSinRepetir, (void*)existeUnaIgual)) {
				list_add(memoriasSinRepetir, memoria);
			}
		}
		// me guardo una lista con puerto e ip sii no existe en la lista
		// porque una memoria puede tener + de 1 criterio
		pthread_mutex_lock(&semMMemoriaSC);
		if(memoriaSc != NULL) {
			list_add(memoriasSinRepetir, memoriaSc);
		}
		pthread_mutex_unlock(&semMMemoriaSC);
		pthread_mutex_lock(&semMMemoriasSHC);
		list_iterate(memoriasShc,(void*)agregarMemoriaSinRepetir);
		pthread_mutex_unlock(&semMMemoriasSHC);
		pthread_mutex_lock(&semMMemoriasEC);
		list_iterate(memoriasEc,(void*)agregarMemoriaSinRepetir);
		pthread_mutex_unlock(&semMMemoriasEC);
		pthread_mutex_lock(&semMMemoriaSC);
		pthread_mutex_lock(&semMMemoriasSHC);
		pthread_mutex_lock(&semMMemoriasEC);
		list_iterate(memoriasSinRepetir, (void*)enviarJournal);
		pthread_mutex_unlock(&semMMemoriaSC);
		pthread_mutex_unlock(&semMMemoriasSHC);
		pthread_mutex_unlock(&semMMemoriasEC);
		list_destroy(memoriasSinRepetir);
	}

}

/* procesarRun()
 * Parametros:
 * 	-> t_queue* :: colaRun
 * Descripcion: recibe una cola de requests, procesa cada una,
 * si es válida la ejecuta. Cuando es fin de quantum, vuelve a ready.
 * Return:
 * 	-> void  */
void procesarRun(t_queue* colaRun) {
	// usar inotify por si cambia Q
	request_procesada* request;
	int quantumActual = 0;
	//el while es fin de Q o fin de cola
	pthread_mutex_lock(&semMQuantum);
	while(!queue_is_empty(colaRun) && quantumActual < quantum) {
		request = (request_procesada*) malloc(sizeof(request_procesada));
		request->codigo = ((request_procesada*) queue_peek(colaRun))->codigo;
		request->request = strdup((char*)((request_procesada*)queue_peek(colaRun))->request);

		if (validarRequest((char*) request->request) == TRUE) {
			if (manejarRequest(request) != SUCCESS) {
				break;
				//libero recursos, mato hilo, lo saco de la cola, e informo error
			}
		} else {
			break;
			//libero recursos, mato hilo, lo saco de la cola, e informo error
		}
		free((char*)((request_procesada*)queue_peek(colaRun))->request);
		free(queue_pop(colaRun));
		liberarRequestProcesada(request);
		quantumActual++;
		usleep(sleepEjecucion*1000);
	}
	if (quantumActual == quantum && queue_is_empty(colaRun) == FALSE) {
		//termino por fin de q
		log_info(logger_KERNEL, "Vuelvo a ready");
		request_procesada* _request = (request_procesada*)(malloc(sizeof(request_procesada)));
		//_request->request = (t_queue*) (malloc(sizeof(t_queue)));
		_request->request = colaRun;
		_request->codigo = RUN;
		pthread_mutex_lock(&semMColaReady);
		queue_push(ready, _request);
		pthread_mutex_unlock(&semMColaReady);
		sem_post(&semRequestReady);
	} else if (queue_is_empty(colaRun) == TRUE){
		// si estoy aca es porque ya ejecuto toda la cola
		log_info(logger_KERNEL, "Finalizó la ejecución del script");
		queue_destroy(colaRun);
	} else {
		log_error(logger_KERNEL, "Se cancelo el script");
		liberarRequestProcesada(request);
		queue_destroy_and_destroy_elements(colaRun, (void*)liberarColaRequest);
		// liberar recursos que ocupa el struct este con sus colas o sea el request - es lo de arriba
	}
}

/* procesarAdd()
 * Parametros:
 * 	-> char* :: mensaje
 * Descripcion: recibe el request y asigna criterio a memoria.
 * Luego le informa a memoria que se le asignó ese criterio.
 * Return:
 * 	-> void  */
int procesarAdd(char* mensaje) {
	int estado = SUCCESS;
	char** requestDividida = separarRequest(mensaje);
	config_memoria* memoria;
	consistencia _consistencia;
	_consistencia = obtenerEnumConsistencia(requestDividida[4]);
	if (_consistencia == CONSISTENCIA_INVALIDA) {
		estado = ERROR_GENERICO;
		log_error(logger_KERNEL, "El criterio %s no es válido", requestDividida[4]);
	}
	int esMemoriaCorrecta(config_memoria* memoriaActual) {
		return string_equals_ignore_case(memoriaActual->numero, requestDividida[2]);
	}
	pthread_mutex_lock(&semMMemorias);
	memoria = (config_memoria*) list_find(memorias, (void*)esMemoriaCorrecta);
	if (memoria == NULL) {
		estado = ERROR_GENERICO;
		log_error(logger_KERNEL, "No encontré la memoria %s", requestDividida[2]);
	} else {
		switch (_consistencia) {
			case SC:
				pthread_mutex_lock(&semMMemoriaSC);
				memoriaSc = memoria;
				log_info(logger_KERNEL, "Se agregó la memoria %s al criterio SC", memoria->numero);
				pthread_mutex_unlock(&semMMemoriaSC);
				break;
			case SHC:
				pthread_mutex_lock(&semMMemoriasSHC);
				list_add(memoriasShc, memoria);
				log_info(logger_KERNEL, "Se agregó la memoria %s al criterio SHC", memoria->numero);
				pthread_mutex_unlock(&semMMemoriasSHC);
				//procesarJournal(TRUE);
				break;
			case EC:
				pthread_mutex_lock(&semMMemoriasEC);
				list_add(memoriasEc, memoria);
				log_info(logger_KERNEL, "Se agregó la memoria %s al criterio EC", memoria->numero);
				pthread_mutex_unlock(&semMMemoriasEC);
				break;
			default:
				break;
		}
	}
	pthread_mutex_unlock(&semMMemorias);
	liberarArrayDeChar(requestDividida);
	return estado;
}
