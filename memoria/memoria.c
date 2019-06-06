#include "memoria.h"
#include <stdlib.h>

int main(void) {

//--------------------------------RESERVA DE MEMORIA------------------------------------------------------------
	//TODO solo se debe reservar el tam max y DELEGARSE A UNA FUNCION
	pag =(t_pagina*) malloc(sizeof(t_pagina));
	elementoA1 =malloc(sizeof(t_elemTablaDePaginas));
	tablaA = malloc(sizeof(t_tablaDePaginas));
	tablaDeSegmentos = malloc(sizeof(t_segmentos));

//--------------------------------AUXILIAR: creacion de tabla/pag/elemento --------------------------------------

	tablaDeSegmentos->segmentos =list_create();
	tablaA->elementosDeTablaDePagina = list_create();	//list_create() HACE UN MALLOC
	tablaA->nombre= strdup("TablaA");
	pag->timestamp = 123456789;
	pag->key=1;
	pag->value=strdup("hola");
	elementoA1->numeroDePag=1;
	elementoA1->pagina= pag;
	elementoA1->modificado = SINMODIFICAR;

	list_add(tablaA->elementosDeTablaDePagina, elementoA1);
	list_add(tablaDeSegmentos->segmentos,tablaA);

//--------------------------------INICIO DE MEMORIA ---------------------------------------------------------------
	config = leer_config("/home/utnso/tp-2019-1c-bugbusters/memoria/memoria.config");
	logger_MEMORIA = log_create("memoria.log", "Memoria", 1, LOG_LEVEL_DEBUG);

//--------------------------------CONEXION CON LFS ---------------------------------------------------------------
	conectarAFileSystem();

//--------------------------------SEMAFOROS-HILOS ----------------------------------------------------------------
	//	SEMAFOROS
	//sem_init(&semLeerDeConsola, 0, 1);
	sem_init(&semEnviarMensajeAFileSystem, 0, 0);
	//pthread_mutex_init(&terminarHilo, NULL);

	// 	HILOS
	pthread_create(&hiloLeerDeConsola, NULL, (void*)leerDeConsola, NULL);
	pthread_create(&hiloEscucharMultiplesClientes, NULL, (void*)escucharMultiplesClientes, NULL);

	pthread_detach(hiloLeerDeConsola);
	pthread_join(hiloEscucharMultiplesClientes, NULL);

//-------------------------------- -PARTE FINAL DE MEMORIA---------------------------------------------------------
	liberar_conexion(conexionLfs);
	//liberarMemoria();
	log_destroy(logger_MEMORIA);
 	config_destroy(config);
 	FD_ZERO(&descriptoresDeInteres);// TODO momentaneamente, asi cerramos todas las conexiones

	return 0;
}

/* leerDeConsola()
 * Parametros:
 * 	->  ::  void
 * Descripcion: Lee la request de consola.
 * 				Valida el caso de que s ehaya ingresado SALIDA (por lo que setea el flagTerminaHiloMultiplesClientes en 1
 * Return:
 * 	-> codPalabraReservada :: void
 * VALGRIND:: SI */
void leerDeConsola(void){
	char* mensaje;
	log_info(logger_MEMORIA, "Vamos a leer de consola");
	while (1) {
		//sem_wait(&semLeerDeConsola);
		mensaje = readline(">");
		if (!strcmp(mensaje, "\0")) {
			break;
		}
//		if(validarRequest(mensaje)== SALIDA){
//			pthread_mutex_lock(&terminarHilo);
//			flagTerminarHiloMultiplesClientes =1;
//			pthread_mutex_unlock(&terminarHilo);
//			break;
//		}

		validarRequest(mensaje);
		free(mensaje);
		mensaje=NULL;
	}
}

/* validarRequest()
 * Parametros:
 * 	->  mensaje :: char*
 * Descripcion: Cachea si el codigo de validacion encontrado.
 * 				En caso de ser un codigo valido, se proede a interpretar la request y se devuelve EXIT_SUCCESS.
 * 				En caso de error se deuvuelve NUESTRO_ERROR y se logea el mismo.
 * 				En caso de de ser salida, se devuelve SALIDA.
 *
 * Return:
 * 	-> resultadoVlidacion :: int
 * VALGRIND:: NO */
int validarRequest(char* mensaje){
	int codValidacion;
	char** request = string_n_split(mensaje, 2, " ");
	codValidacion = validarMensaje(mensaje, MEMORIA, logger_MEMORIA);
	cod_request palabraReservada = obtenerCodigoPalabraReservada(request[0],MEMORIA);
	switch(codValidacion){
		case EXIT_SUCCESS:
			interpretarRequest(palabraReservada, mensaje, CONSOLE,-1);
			return EXIT_SUCCESS;
			break;
		case NUESTRO_ERROR:
			//TODO es la q hay q hacerla generica
			log_error(logger_MEMORIA, "La request no es valida");
			return NUESTRO_ERROR;
			break;
		case SALIDA:
			return SALIDA;
			break;
		default:
			return NUESTRO_ERROR;
			break;
	}
	liberarArrayDeChar(request);
	request =NULL;
}

/* conectarAFileSystem()
 * Parametros:
 * 	->  :: void
 * Descripcion: Se crea la conexion con lissandraFileSystem
 *
 * Return:
 * 	-> :: void
 * VALGRIND:: SI */
void conectarAFileSystem() {
	conexionLfs = crearConexion(
			config_get_string_value(config, "IP_LFS"),
			config_get_string_value(config, "PUERTO_LFS"));
	log_info(logger_MEMORIA, "SE CONECTO CN LFS");
}

void escucharMultiplesClientes() {
	int descriptorServidor = iniciar_servidor(config_get_string_value(config, "PUERTO"), config_get_string_value(config, "IP"));
	log_info(logger_MEMORIA, "Memoria lista para recibir al kernel");

	/* fd = file descriptor (id de Socket)
	 * fd_set es Set de fd's (una coleccion)*/

	descriptoresClientes = list_create();	// Lista de descriptores de todos los clientes conectados (podemos conectar infinitos clientes)
	int numeroDeClientes = 0;				// Cantidad de clientes conectados
	int valorMaximo = 0;					// Descriptor cuyo valor es el mas grande (para pasarselo como parametro al select)
	t_paquete* paqueteRecibido;

	while(1) {
		//Varibale global, el hilo de leerConsola, cuando reccibe "SALIDA" lo setea en 1
//		pthread_mutex_lock(&terminarHilo);
//		if(flagTerminarHiloMultiplesClientes ==1){
//			pthread_mutex_unlock(&terminarHilo);
//			log_info(logger_MEMORIA,"ENTRE AL WHILE 1");
//			break;
//		}else{
//			pthread_mutex_unlock(&terminarHilo);

			eliminarClientesCerrados(descriptoresClientes, &numeroDeClientes);	// Se eliminan los clientes que tengan un -1 en su fd
			FD_ZERO(&descriptoresDeInteres); 									// Inicializamos descriptoresDeInteres
			FD_SET(descriptorServidor, &descriptoresDeInteres);					// Agregamos el descriptorServidor a la lista de interes

			for (int i=0; i< numeroDeClientes; i++) {
				FD_SET((int) list_get(descriptoresClientes,i), &descriptoresDeInteres); // Agregamos a la lista de interes, los descriptores de los clientes
			}

			valorMaximo = maximo(descriptoresClientes, descriptorServidor, numeroDeClientes); // Se el valor del descriptor mas grande. Si no hay ningun cliente, devuelve el fd del servidor
			select(valorMaximo + 1, &descriptoresDeInteres, NULL, NULL, NULL); 				  // Espera hasta que algún cliente tenga algo que decir

			for(int i=0; i<numeroDeClientes; i++) {
				if (FD_ISSET((int) list_get(descriptoresClientes,i), &descriptoresDeInteres)) {   // Se comprueba si algún cliente ya conectado mando algo
					paqueteRecibido = recibir((int) list_get(descriptoresClientes,i)); // Recibo de ese cliente en particular
					cod_request palabraReservada = paqueteRecibido->palabraReservada;
					char* request = paqueteRecibido->request;
					printf("El codigo que recibi es: %s \n", request);
					printf("Del fd %i \n", (int) list_get(descriptoresClientes,i)); // Muestro por pantalla el fd del cliente del que recibi el mensaje
					interpretarRequest(palabraReservada,request,ANOTHER_COMPONENT, i);
					free(request);
					request=NULL;

				}
			}//fin for

			if(FD_ISSET (descriptorServidor, &descriptoresDeInteres)) {
				int descriptorCliente = esperar_cliente(descriptorServidor); 					  // Se comprueba si algun cliente nuevo se quiere conectar
				numeroDeClientes = (int) list_add(descriptoresClientes, (int*) descriptorCliente); // Agrego el fd del cliente a la lista de fd's
				numeroDeClientes++;
			}
	//	}
	}
	eliminar_paquete(paqueteRecibido);
	paqueteRecibido=NULL;

}

void interpretarRequest(cod_request palabraReservada,char* request,t_caller caller, int i) {

log_info(logger_MEMORIA,"entre a interpretarr request");
	switch(palabraReservada) {

		case SELECT:
			log_info(logger_MEMORIA, "Me llego un SELECT");
			procesarSelect(palabraReservada, request, caller, i);
			break;
		case INSERT:
			log_info(logger_MEMORIA, "Me llego un INSERT");
			procesarInsert(palabraReservada, request, caller, i);
			break;
		case CREATE:
			log_info(logger_MEMORIA, "Me llego un CREATE");
			break;
		case DESCRIBE:
			log_info(logger_MEMORIA, "Me llego un DESCRIBE");
			break;
		case DROP:
			log_info(logger_MEMORIA, "Me llego un DROP");
			break;
		case JOURNAL:
			log_info(logger_MEMORIA, "Me llego un JOURNAL");
			break;
		case SALIDA:
			log_info(logger_MEMORIA,"HaS finalizado el componente MEMORIA");
			break;
		case NUESTRO_ERROR:
			if(caller == ANOTHER_COMPONENT){
				log_error(logger_MEMORIA, "el cliente se desconecto. Terminando servidor");
				int valorAnterior = (int) list_replace(descriptoresClientes, i, (int*) -1); // Si el cliente se desconecta le pongo un -1 en su fd}
				// TODO: Chequear si el -1 se puede castear como int*
				break;
			}
			else{
				break;
			}
		default:
			log_warning(logger_MEMORIA, "Operacion desconocida. No quieras meter la pata");
			break;
	}
}

/*intercambiarConFileSystem()
 * Parametros:
 * 	-> cod_request :: palabraReservada
 * 	-> char* ::  request
 * Descripcion: Permite enviar la request (recibida por parametro) a LFS y espera la respuesta de ella.
 * Return:
 * 	-> paqueteRecibido:: char* */
t_paquete* intercambiarConFileSystem(cod_request palabraReservada, char* request){
	t_paquete* paqueteRecibido;

	enviar(palabraReservada, request, conexionLfs);
	//sem_post(&semLeerDeConsola);
	paqueteRecibido = recibir(conexionLfs);

	return paqueteRecibido;

}

/*procesarSelect()
 * Parametros:
 * 	-> char* ::  request
 * 	-> cod_request :: palabraReservada
 * Descripcion: Permite obtener el valor de la key consultada de una tabla.
 * Return:
 * 	-> :: void */
void procesarSelect(cod_request palabraReservada, char* request, t_caller caller, int i) {

//---------------CASOS DE PRUEBA------------------------------

	t_paquete* valorDeLF=malloc(sizeof(t_paquete));
	valorDeLF->palabraReservada= 0;
	valorDeLF->tamanio=100;
	valorDeLF->request=strdup("tablaB 2 chau 454462636");
//-------------------------------------------------------------
	t_elemTablaDePaginas* elementoEncontrado = malloc(sizeof(t_elemTablaDePaginas));
	t_paquete* valorEncontrado=malloc(sizeof(t_paquete));


	int resultadoCache;

	switch(consistenciaMemoria){
		case SC:
		case SHC:
			log_info(logger_MEMORIA,"ME LO TIENE QUE DECIR LFS");
			//TODO CUANDO LFS PUEDA HACER INSERT CORERCTAMENTE, HAY QUE DESCOMNTARLO
			//valorDeLFS = intercambiarConFileSystem(palabraReservada,request);
			enviarAlDestinatarioCorrecto(palabraReservada, valorDeLF->palabraReservada,request, valorDeLF,caller, (int) list_get(descriptoresClientes,i));
			resultadoCache= estaEnMemoria(palabraReservada, request,&valorEncontrado,&elementoEncontrado);
			guardarRespuestaDeLFSaCACHE(valorDeLF, resultadoCache);
			break;
		case EC:		// en caso de no existir el segmento o la tabla en MEMORIA, se lo solicta a LFS
			resultadoCache= estaEnMemoria(palabraReservada, request,&valorEncontrado,&elementoEncontrado);
			if(resultadoCache == EXIT_SUCCESS ) {
				log_info(logger_MEMORIA, "LO ENCONTRE EN CACHEE!");
				enviarAlDestinatarioCorrecto(palabraReservada, SUCCESS,request, valorEncontrado,caller, (int) list_get(descriptoresClientes,i));
				free(valorEncontrado);
				valorEncontrado=NULL;
			} else {// en caso de no existir el segmento o la tabla en MEMORIA, se lo solicta a LFS
				log_info(logger_MEMORIA,"ME LO TIENE QUE DECIR LFS");
				//valorDeLFS = intercambiarConFileSystem(palabraReservada,request);
				enviarAlDestinatarioCorrecto(palabraReservada, valorDeLF->palabraReservada,request, valorDeLF,caller, (int) list_get(descriptoresClientes,i));
				guardarRespuestaDeLFSaCACHE(valorDeLF, resultadoCache);
				free(valorEncontrado);
				valorEncontrado=NULL;
			}
			break;
		default:
			log_info(logger_MEMORIA, "NO se le ha asignado un tipo de consistencia a la memoria, por lo que no puede responder la consulta: ", request);
			free(elementoEncontrado);
			elementoEncontrado=NULL;
			break;
	}
	free(valorDeLF);
	valorDeLF=NULL;
}



/*estaEnMemoria()
 * Parametros:
 * 	-> cod_request :: palabraReservada
 * 	-> char* :: request
 * 	-> char** :: valorEncontrado- se modifica por referencia
 * 	-> t_elemTablaDePaginas** :: elemento (que contiene el puntero a la pag econtrada)- se modifica por referecia.
 * Descripcion: Revisa si la tabla y key solicitada se encuentran que cache.
 * 				En caso de que sea cierto, modifica valorEncontrado y elementoEncontrado; devuelce operacion exitosa.
 * 				En ccaso de que no exista la tabla||key devuelve el error correspondiente.
 * Return:
 * 	-> resultado de la operacion:: int */
int estaEnMemoria(cod_request palabraReservada, char* request,t_paquete** valorEncontrado,t_elemTablaDePaginas** elementoEncontrado){
	t_tablaDePaginas* tablaDeSegmentosEnCache = malloc(sizeof(t_tablaDePaginas));
	t_elemTablaDePaginas* elementoDePagEnCache = malloc(sizeof(t_elemTablaDePaginas));
	t_paquete* paqueteAuxiliar;

	char** parametros = separarRequest(request);
	char* segmentoABuscar=strdup(parametros[1]);
	uint16_t keyABuscar= convertirKey(parametros[2]);

	int encontrarTabla(t_tablaDePaginas* tablaDePaginas){
		return string_equals_ignore_case(tablaDePaginas->nombre, segmentoABuscar);
	}

	tablaDeSegmentosEnCache= list_find(tablaDeSegmentos->segmentos,(void*)encontrarTabla);
	if(tablaDeSegmentosEnCache!= NULL){

		int encontrarElemDePag(t_elemTablaDePaginas* elemDePagina){
			return (elemDePagina->pagina->key == keyABuscar);
		}

		elementoDePagEnCache= list_find(tablaDeSegmentosEnCache->elementosDeTablaDePagina,(void*)encontrarElemDePag);
		if(elementoDePagEnCache !=NULL){ //registro = pagina
			paqueteAuxiliar=malloc(sizeof(t_paquete));
			paqueteAuxiliar->palabraReservada=SUCCESS;
			char* requestAEnviar= strdup("");
			string_append_with_format(&requestAEnviar,"%s%s%s%s%c%s%c",segmentoABuscar," ",parametros[2]," ",'"',elementoDePagEnCache->pagina->value,'"');
			paqueteAuxiliar->request = strdup(requestAEnviar);
			paqueteAuxiliar->tamanio=sizeof(elementoDePagEnCache->pagina->value);

			*elementoEncontrado=elementoDePagEnCache;
			*valorEncontrado = paqueteAuxiliar;
//			free(tablaDeSegmentosEnCache);
//			tablaDeSegmentosEnCache=NULL;
//			free(elementoDePagEnCache);
//			elementoDePagEnCache=NULL;
			free(segmentoABuscar);
			segmentoABuscar=NULL;
			free(requestAEnviar);
			requestAEnviar=NULL;
			return EXIT_SUCCESS;
		}else{
//			free(tablaDeSegmentosEnCache);
//			tablaDeSegmentosEnCache=NULL;
			free(elementoDePagEnCache);
			elementoDePagEnCache=NULL;
			free(segmentoABuscar);
			segmentoABuscar=NULL;
			return KEYINEXISTENTE;
		}
		free(tablaDeSegmentosEnCache);
		tablaDeSegmentosEnCache=NULL;
		free(elementoDePagEnCache);
		elementoDePagEnCache=NULL;
		free(segmentoABuscar);
		segmentoABuscar=NULL;
	}else{
		free(tablaDeSegmentosEnCache);
		tablaDeSegmentosEnCache=NULL;
		free(elementoDePagEnCache);
		elementoDePagEnCache=NULL;
		free(segmentoABuscar);
		segmentoABuscar=NULL;
		return SEGMENTOINEXISTENTE;
	}
	free(tablaDeSegmentosEnCache);
	tablaDeSegmentosEnCache=NULL;
	free(elementoDePagEnCache);
	elementoDePagEnCache=NULL;
	free(segmentoABuscar);
	segmentoABuscar=NULL;
	eliminar_paquete(paqueteAuxiliar);
}


t_tablaDePaginas* encontrarSegmento(char* segmentoABuscar){
	int encontrarTabla(t_tablaDePaginas* tablaDePaginas){
		return string_equals_ignore_case(tablaDePaginas->nombre, segmentoABuscar);
	}

	return list_find(tablaDeSegmentos->segmentos,(void*)encontrarTabla);
}

/* enviarAlDestinatarioCorrecto()
 * Parametros:
 * 	-> char* ::  request
 * 	-> cod_request :: palabraReservada
 * Descripcion: se va a fijar si existe el segmento de la tabla ,que se quiere hacer insert,
 * 				en la memoria principal.
 * 				Si Existe dicho segmento, busca la key (y de encontrarla,
 * 				actualiza su valor insertando el timestap actual).Y si no encuentra, solicita pag
 * 				y la crea. Pero de no haber pag suficientes, se hace journaling.
 * 				Si no se encuentra el segmento,solicita un segment para crearlo y lo hace.Y, en
 * Return:
 * 	-> :: void */
 void enviarAlDestinatarioCorrecto(cod_request palabraReservada,int codResultado,char* request,t_paquete* valorAEnviar,t_caller caller,int socketKernel){
	 //TODO reservar y liberar
	 char *errorDefault= strdup("");
	 switch(caller){
	 	 case(ANOTHER_COMPONENT):
			enviar(codResultado, valorAEnviar->request, socketKernel);
	 	 	break;
	 	 case(CONSOLE):
	 		mostrarResultadoPorConsola(palabraReservada, codResultado,request, valorAEnviar);
	 	  	break;
	 	 default:
	 		string_append_with_format(&errorDefault, "%s%s","No se ha encontrado a quien devolver la reques realizada",request);
	 		 log_info(logger_MEMORIA,errorDefault);
	 		 break;

	}
	free(errorDefault);
	errorDefault=NULL;
 }

 void mostrarResultadoPorConsola(cod_request palabraReservada, int codResultado,char* request,t_paquete* valorAEnviar){
	 char* respuesta= strdup("");
	 char* error=strdup("");
	 char** requestSeparada=separarRequest(valorAEnviar->request);
	 char* valorEncontrado = requestSeparada[2];
	 switch(palabraReservada){
	 	 case(SELECT):
	 		if(codResultado == SUCCESS){
				string_append_with_format(&respuesta, "%s%s%s%s","La respuesta a la request: ",request," es: ", valorEncontrado);
				log_info(logger_MEMORIA,respuesta);
	 			free(respuesta);
	 			respuesta=NULL;
	 			free(error);
	 			error=NULL;
	 		}else{
	 			switch(codResultado){
					case(KEY_NO_EXISTE):
						string_append_with_format(&error, "%s%s%s","La respuesta a: ",request," no es valida, KEY INEXISTENTE");
						log_info(logger_MEMORIA,error);
						break;
					case(TABLA_NO_EXISTE):
						string_append_with_format(&error, "%s%s%s","La respuesta a: ",request," no es valida, TABLA INEXISTENTE");
						log_info(logger_MEMORIA,error);
						break;
					default:
						log_info(logger_MEMORIA,"No se ha podido encontrar respuesta a la request",request);
						break;
	 			}
	 			free(respuesta);
	 			respuesta=NULL;
	 			free(error);
	 			error=NULL;
			}
	 	 break;
		case(INSERT):
			if(codResultado == SUCCESS){
				string_append_with_format(&respuesta, "%s%s%s","La request: ",request," se ha realizado con exito");
				log_info(logger_MEMORIA,respuesta);
	 			free(respuesta);
	 			respuesta=NULL;
	 			free(error);
	 			error=NULL;
			}else{//TODO CREO Q SOLO ME PUEDEN DECIR Q NO EXITE LA TABLA
				string_append_with_format(&error, "%s%s%s","La request: ",request," no a podido realizarse, TABLA INEXISTENTE");
				log_info(logger_MEMORIA,error);
	 			free(respuesta);
	 			respuesta=NULL;
	 			free(error);
	 			error=NULL;
			}
			break;
		default:
			log_info(logger_MEMORIA,"MEMORIA NO LO SABE RESOLVER AUN, PERO TE INVITO A QUE LO HAGAS VOS :)");
 			free(respuesta);
 			respuesta=NULL;
 			free(error);
 			error=NULL;
			break;
		}
}


void guardarRespuestaDeLFSaCACHE(t_paquete* nuevoPaquete,t_erroresCache tipoError){

	if(nuevoPaquete->palabraReservada == SUCCESS){
		char** requestSeparada= separarRequest(nuevoPaquete->request);
		char* nuevaTabla= strdup(requestSeparada[0]);
		uint16_t nuevaKey= convertirKey(requestSeparada[1]);
		char* nuevoValor= strdup(requestSeparada[2]);
		unsigned long long nuevoTimestamp;
		int rta=convertirTimestamp(requestSeparada[3],&nuevoTimestamp);//no checkeo, viene de LFS
		if(tipoError== KEYINEXISTENTE){
			t_tablaDePaginas* tablaBuscada= malloc(sizeof(t_tablaDePaginas));
			tablaBuscada= encontrarSegmento(nuevaTabla);
			list_add(tablaBuscada->elementosDeTablaDePagina,crearElementoEnTablaDePagina(nuevaKey, nuevoValor,nuevoTimestamp));
			free(tablaBuscada);
			tablaBuscada=NULL;
			free(nuevaTabla);
			nuevaTabla=NULL;
			free(nuevoValor);
			nuevoValor=NULL;
		}else if(tipoError==SEGMENTOINEXISTENTE){
			t_tablaDePaginas* nuevaTablaDePagina = crearTablaDePagina(nuevaTabla);
			list_add(nuevaTablaDePagina->elementosDeTablaDePagina,crearElementoEnTablaDePagina(nuevaKey,nuevoValor,nuevoTimestamp));
			list_add(tablaDeSegmentos->segmentos,nuevaTablaDePagina);
			free(nuevaTabla);
			nuevaTabla=NULL;
			free(nuevoValor);
			nuevoValor=NULL;
		}
	}
//		case(TABLA_NO_EXISTE):
//		case(KEY_NO_EXISTE):

}

/* procesarInsert()
 * Parametros:
 * 	-> cod_request :: palabraReservada
 *	-> char* :: request
 *	->
 * Descripcion: se va a fijar si existe el segmento de la tabla ,que se quiere hacer insert,
 * 				en la memoria principal.
 * 				Si Existe dicho segmento, busca la key (y de encontrarla,
 * 				actualiza su valor insertando el timestap actual).Y si no encuentra, solicita pag
 * 				y la crea. Pero de no haber pag suficientes, se hace journaling.
 * 				Si no se encuentra el segmento,solicita un segment para crearlo y lo hace.Y, en
 * Return:
 * 	-> :: void */
void procesarInsert(cod_request palabraReservada, char* request, t_caller caller, int i) {
		t_elemTablaDePaginas* elementoEncontrado= malloc(sizeof(t_elemTablaDePaginas));
		t_paquete* valorEncontrado=malloc(sizeof(t_paquete));

//---------------CASOS DE PRUEBA------------------------------
//en el .h para poder compartirlo con la funcion insertar
		valorDeLF=malloc(sizeof(t_paquete));
		valorDeLF->palabraReservada= 0;
		valorDeLF->tamanio=100;
		valorDeLF->request=strdup("tablaB  chau 123454657");

//-------------------------------------------------------------

		puts("ANTES DE IR A BUSCAR A CACHE");
		int resultadoCache= estaEnMemoria(palabraReservada, request,&valorEncontrado,&elementoEncontrado);
		switch(consistenciaMemoria){
			case SC:
			case SHC:
				//valorDeLFS = intercambiarConFileSystem(SELECT,consultaALFS);
				if((consistenciaMemoria== SC && validarInsertSC(valorDeLF->palabraReservada)== EXIT_SUCCESS)){
					insertar(resultadoCache,palabraReservada,request,elementoEncontrado,caller,i);
					free(elementoEncontrado);
					elementoEncontrado=NULL;
					free(valorEncontrado);
					valorEncontrado=NULL;
				}else{
					enviarAlDestinatarioCorrecto(palabraReservada,valorDeLF->palabraReservada,request,valorDeLF,caller, (int) list_get(descriptoresClientes,i));
					free(elementoEncontrado);
					elementoEncontrado=NULL;
					free(valorEncontrado);
					valorEncontrado=NULL;
				}
				break;
			case EC:
				insertar(resultadoCache,palabraReservada,request,elementoEncontrado,caller,i);
//				free(elementoEncontrado);
//				elementoEncontrado=NULL;
				free(valorEncontrado);
				valorEncontrado=NULL;
				break;
		}
		free(valorDeLF);
		valorDeLF=NULL;
}


void insertar(int resultadoCache,cod_request palabraReservada,char* request,t_elemTablaDePaginas* elementoEncontrado,t_caller caller, int i){
	t_paquete* paqueteAEnviar= malloc(sizeof(t_paquete));

	char** parametros = separarRequest(request);
	char* nuevaTabla = strdup(parametros[1]);
	char* nuevaKeyChar = strdup(parametros[2]);
	int nuevaKey = convertirKey(nuevaKeyChar);
	char* nuevoValor = strdup(parametros[3]);
	unsigned long long nuevoTimestamp;

	char *consultaALFS= strdup("");

	if(parametros[4]!=NULL){
		int rta	= convertirTimestamp(parametros[4],&nuevoTimestamp);
	}else{
		nuevoTimestamp= obtenerHoraActual();
	}


	if(resultadoCache == EXIT_SUCCESS){
		log_info(logger_MEMORIA, "LO ENCONTRE EN CACHEE!");

		actualizarElementoEnTablaDePagina(elementoEncontrado,nuevoValor);

		paqueteAEnviar->palabraReservada= SUCCESS;
		char** requestRespuesta= string_n_split(request,2," ");
		paqueteAEnviar->request=strdup(requestRespuesta[1]);
		paqueteAEnviar->tamanio=strlen(requestRespuesta[1]);
		enviarAlDestinatarioCorrecto(palabraReservada, SUCCESS,request, paqueteAEnviar,caller, (int) list_get(descriptoresClientes,i));
		//free(paqueteAEnviar)
		free(nuevaTabla);
		nuevaTabla=NULL;
		free(nuevoValor);
		nuevoValor=NULL;
	}else if(resultadoCache == KEYINEXISTENTE){//TODO:		KEY no encontrada -> nueva pagina solicitada
		int hayEspacio= EXIT_SUCCESS;
		//TODO verficar realmente si se puede insertar
		if(hayEspacio ==EXIT_SUCCESS){
			t_tablaDePaginas* tablaDestino = (t_tablaDePaginas*)malloc(sizeof(t_tablaDePaginas));
			tablaDestino = encontrarSegmento(nuevaTabla);
			list_add(tablaDestino->elementosDeTablaDePagina,crearElementoEnTablaDePagina(nuevaKey,nuevoValor, nuevoTimestamp));

			//TODO unificar desp cuando lfs este, con valorDeLF

			paqueteAEnviar->palabraReservada= SUCCESS;
			char** requestRespuesta= string_n_split(request,2," ");
			paqueteAEnviar->request=strdup(requestRespuesta[1]);
			paqueteAEnviar->tamanio=strlen(requestRespuesta[1]);


			enviarAlDestinatarioCorrecto(palabraReservada, SUCCESS,request, paqueteAEnviar,caller, (int) list_get(descriptoresClientes,i));

			//free(paqueteAEnviar) TODO
			free(nuevaTabla);
			nuevaTabla=NULL;
			free(nuevoValor);
			nuevoValor=NULL;
//			free(tablaDestino);
//			tablaDestino=NULL;
			free(requestRespuesta);
			requestRespuesta=NULL;
		}

	}else if(resultadoCache == SEGMENTOINEXISTENTE){ //	TABLA no encontrada -> nuevo segmento

			t_tablaDePaginas* nuevaTablaDePagina = crearTablaDePagina(nuevaTabla);
			list_add(tablaDeSegmentos->segmentos,nuevaTablaDePagina);
			list_add(nuevaTablaDePagina->elementosDeTablaDePagina,crearElementoEnTablaDePagina(nuevaKey,nuevoValor,nuevoTimestamp));
			enviarAlDestinatarioCorrecto(palabraReservada, SUCCESS,request, valorDeLF,caller, (int) list_get(descriptoresClientes,i));
			//free(paqueteAEnviar) TODO
			free(nuevaTabla);
			nuevaTabla=NULL;
			free(nuevoValor);
			nuevoValor=NULL;
	}
}


int validarInsertSC(errorNo codRespuestaDeLFS){
	if (codRespuestaDeLFS == SUCCESS){
		return EXIT_SUCCESS;
	}else{
		return EXIT_FAILURE;
	}
}

t_pagina* crearPagina(uint16_t nuevaKey, char* nuevoValor, unsigned long long nuevoTimesTamp){
	t_pagina* nuevaPagina= (t_pagina*)malloc(sizeof(t_pagina));
	nuevaPagina->timestamp = nuevoTimesTamp;
	nuevaPagina->key = nuevaKey;
	nuevaPagina->value = nuevoValor;
	return nuevaPagina;
}

void actualizarPagina (t_pagina* pagina, char* newValue){
	unsigned long long newTimes = obtenerHoraActual();
	pagina->timestamp = newTimes;
	pagina->value =strdup(newValue);
}

t_elemTablaDePaginas* crearElementoEnTablaDePagina(uint16_t newKey, char* newValue, unsigned long long timesTamp){
	t_elemTablaDePaginas* newElementoDePagina= (t_elemTablaDePaginas*)malloc(sizeof(t_elemTablaDePaginas));
	newElementoDePagina->numeroDePag = rand();
	newElementoDePagina->pagina = crearPagina(newKey,newValue,timesTamp);
	newElementoDePagina->modificado = SINMODIFICAR;
	return newElementoDePagina;
}

void actualizarElementoEnTablaDePagina(t_elemTablaDePaginas* elemento, char* newValue){
	actualizarPagina(elemento->pagina,newValue);
	elemento->modificado = MODIFICADO;
}

t_tablaDePaginas* crearTablaDePagina(char* nuevaTabla){
	t_tablaDePaginas* newTablaDePagina = (t_tablaDePaginas*)malloc(sizeof(t_tablaDePaginas));
	newTablaDePagina->nombre=strdup(nuevaTabla);
	newTablaDePagina->elementosDeTablaDePagina=list_create();
	return newTablaDePagina;
}


// FUNCION QUE QUEREMOS UTILIZAR CUANDO FINALIZAN LOS DOS HILOS
void liberarMemoria(){
	void liberarElementoDePag(t_elemTablaDePaginas* self){
		 free(self->pagina->value);
		 free(self->pagina);
	 }
	list_clean_and_destroy_elements(tablaA->elementosDeTablaDePagina, (void*)liberarElementoDePag);
}


void eliminarElemTablaDePaginas(t_elemTablaDePaginas* elementoEncontrado){
	eliminarPagina(elementoEncontrado->pagina);
	free(elementoEncontrado);
	elementoEncontrado=NULL;
}
void eliminarPagina(t_pagina* pag){
	free(pag->value);
	free(pag);
	pag= NULL;
}
