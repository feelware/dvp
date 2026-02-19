/*
 * RabbitMQ Consumer for MPI Master Node
 *
 * Este programa:
 * 1. Se conecta a RabbitMQ
 * 2. Escucha mensajes de la cola 'video_jobs'
 * 3. Cuando recibe un mensaje, invoca procesamiento MPI
 *
 * Compilar:
 * gcc -o rabbitmq_consumer rabbitmq_consumer.c -lrabbitmq -lcjson
 */

#include <amqp.h>
#include <amqp_tcp_socket.h>
#include <cjson/cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

// Configuración de RabbitMQ (leerá de variables de entorno)
#define QUEUE_NAME "video_jobs"

/**
 * Función para procesar un mensaje recibido
 * Parsea el JSON y extrae los campos necesarios
 */
void process_message(const char *message, size_t message_len) {
  printf("\n========================================\n");
  printf("📨 MENSAJE RECIBIDO DE LA COLA\n");
  printf("========================================\n");
  printf("Contenido: %.*s\n", (int)message_len, message);
  printf("Longitud: %zu bytes\n", message_len);
  printf("========================================\n\n");
  fflush(stdout);

  // Parse del JSON
  cJSON *json = cJSON_ParseWithLength(message, message_len);
  if (json == NULL) {
    const char *error_ptr = cJSON_GetErrorPtr();
    if (error_ptr != NULL) {
      fprintf(stderr, "❌ Error parseando JSON: %s\n", error_ptr);
    }
    return;
  }

  // Extraer campos del JSON
  const cJSON *job_id = cJSON_GetObjectItemCaseSensitive(json, "job_id");
  const cJSON *video_path =
      cJSON_GetObjectItemCaseSensitive(json, "video_path");
  const cJSON *task = cJSON_GetObjectItemCaseSensitive(json, "task");
  const cJSON *params = cJSON_GetObjectItemCaseSensitive(json, "params");

  // Validar que los campos obligatorios existan
  if (!cJSON_IsString(job_id) || !cJSON_IsString(video_path) ||
      !cJSON_IsString(task)) {
    fprintf(
        stderr,
        "❌ Error: Faltan campos obligatorios (job_id, video_path, task)\n");
    cJSON_Delete(json);
    return;
  }

  // Imprimir información extraída
  printf("📋 Información del Job:\n");
  printf("   Job ID: %s\n", job_id->valuestring);
  printf("   Video Path: %s\n", video_path->valuestring);
  printf("   Task: %s\n", task->valuestring);

  if (cJSON_IsObject(params)) {
    char *params_str = cJSON_Print(params);
    printf("   Params: %s\n", params_str);
    free(params_str);
  }
  printf("\n");

  // Preparar params como string para el comando
  char *params_str = NULL;
  if (cJSON_IsObject(params)) {
    params_str = cJSON_Print(params);
  }
  if (!params_str) {
    params_str = strdup("{}");
  }

  // Ejecutar el comando MPI como el usuario mpiuser (usa /home/mpiuser/.ssh)
  // Esto evita que mpirun intente SSH como root y falle por
  // host-key/credenciales
  char command[4096];
  snprintf(
      command, sizeof(command),
      "su - mpiuser -c 'mpirun --allow-run-as-root --mca btl_tcp_if_include "
      "eth0 --mca oob_tcp_if_include eth0 --mca routed direct "
      "-np 6 -H master:2,worker1:2,worker2:2 /usr/local/bin/process_video %s "
      "%s %s \"%s\" 2>&1 | tee /var/log/mpi_jobs/%s.log'",
      job_id->valuestring, video_path->valuestring, task->valuestring,
      params_str, job_id->valuestring);

  printf("Ejecutando: %s\n", command);
  fflush(stdout);

  int result = system(command);
  if (result == 0) {
    printf("Procesamiento completado exitosamente\n");
  } else {
    fprintf(stderr, "Error en procesamiento (codigo: %d)\n", result);
  }

  printf("Mensaje procesado\n\n");
  fflush(stdout);

  // Liberar memoria
  free(params_str);
  cJSON_Delete(json);
}

/**
 * Verifica si hubo error en operaciones de RabbitMQ
 */
int check_amqp_error(amqp_rpc_reply_t reply, const char *context) {
  switch (reply.reply_type) {
  case AMQP_RESPONSE_NORMAL:
    return 0; // Sin error

  case AMQP_RESPONSE_NONE:
    fprintf(stderr, "❌ Error en %s: Sin respuesta\n", context);
    return 1;

  case AMQP_RESPONSE_LIBRARY_EXCEPTION:
    fprintf(stderr, "❌ Error en %s: %s\n", context,
            amqp_error_string2(reply.library_error));
    return 1;

  case AMQP_RESPONSE_SERVER_EXCEPTION:
    switch (reply.reply.id) {
    case AMQP_CONNECTION_CLOSE_METHOD: {
      amqp_connection_close_t *m =
          (amqp_connection_close_t *)reply.reply.decoded;
      fprintf(stderr, "❌ Error en %s: Conexión cerrada por servidor: %.*s\n",
              context, (int)m->reply_text.len, (char *)m->reply_text.bytes);
      break;
    }
    case AMQP_CHANNEL_CLOSE_METHOD: {
      amqp_channel_close_t *m = (amqp_channel_close_t *)reply.reply.decoded;
      fprintf(stderr, "❌ Error en %s: Canal cerrado por servidor: %.*s\n",
              context, (int)m->reply_text.len, (char *)m->reply_text.bytes);
      break;
    }
    default:
      fprintf(stderr, "❌ Error en %s: Respuesta desconocida del servidor\n",
              context);
      break;
    }
    return 1;
  }

  return 0;
}

int main(int argc, char *argv[]) {
  // Desactivar buffering para que los logs se escriban inmediatamente
  setbuf(stdout, NULL);
  setbuf(stderr, NULL);

  printf("🚀 Iniciando RabbitMQ Consumer para MPI...\n");
  fflush(stdout);

  // Variables de conexión (leer de entorno o usar defaults)
  const char *rabbitmq_host = getenv("RMQ_HOST");
  const char *rabbitmq_user = getenv("RMQ_USER");
  const char *rabbitmq_password = getenv("RMQ_PASSWORD");
  int rabbitmq_port = 5672;

  if (getenv("RMQ_PORT")) {
    rabbitmq_port = atoi(getenv("RMQ_PORT"));
  }

  // Defaults si no hay variables de entorno
  if (!rabbitmq_host)
    rabbitmq_host = "rabbitmq";
  if (!rabbitmq_user)
    rabbitmq_user = "guest";
  if (!rabbitmq_password)
    rabbitmq_password = "guest";

  printf("🚀 Iniciando RabbitMQ Consumer para MPI Master\n");
  printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
  printf("📡 Host: %s:%d\n", rabbitmq_host, rabbitmq_port);
  printf("👤 Usuario: %s\n", rabbitmq_user);
  printf("📬 Cola: %s\n", QUEUE_NAME);
  printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");
  fflush(stdout);

  // 1. Crear conexión
  amqp_connection_state_t conn = amqp_new_connection();
  amqp_socket_t *socket = amqp_tcp_socket_new(conn);
  if (!socket) {
    fprintf(stderr, "❌ Error: No se pudo crear socket TCP\n");
    fflush(stderr);
    return 1;
  }

  printf("🔌 Conectando a RabbitMQ...\n");
  fflush(stdout);
  int status = amqp_socket_open(socket, rabbitmq_host, rabbitmq_port);
  if (status) {
    fprintf(stderr, "❌ Error: No se pudo abrir conexión TCP\n");
    fflush(stderr);
    return 1;
  }
  printf("✅ Conectado al servidor\n\n");
  fflush(stdout);

  // 2. Login
  amqp_rpc_reply_t reply =
      amqp_login(conn,
                 "/",    // vhost
                 0,      // channel_max
                 131072, // frame_max
                 0,      // heartbeat
                 AMQP_SASL_METHOD_PLAIN, rabbitmq_user, rabbitmq_password);

  if (check_amqp_error(reply, "Login")) {
    amqp_destroy_connection(conn);
    return 1;
  }
  printf("✅ Login exitoso\n");

  // 3. Abrir canal
  amqp_channel_open(conn, 1);
  reply = amqp_get_rpc_reply(conn);
  if (check_amqp_error(reply, "Abrir canal")) {
    amqp_connection_close(conn, AMQP_REPLY_SUCCESS);
    amqp_destroy_connection(conn);
    return 1;
  }
  printf("✅ Canal abierto\n");

  // 4. Declarar cola (si no existe, la crea)
  amqp_queue_declare_ok_t *queue_declare =
      amqp_queue_declare(conn,
                         1, // canal
                         amqp_cstring_bytes(QUEUE_NAME),
                         0, // passive
                         1, // durable (persiste después de reiniciar RabbitMQ)
                         0, // exclusive
                         0, // auto-delete
                         amqp_empty_table);
  reply = amqp_get_rpc_reply(conn);
  if (check_amqp_error(reply, "Declarar cola")) {
    amqp_channel_close(conn, 1, AMQP_REPLY_SUCCESS);
    amqp_connection_close(conn, AMQP_REPLY_SUCCESS);
    amqp_destroy_connection(conn);
    return 1;
  }
  printf("✅ Cola '%s' declarada (mensajes en cola: %d)\n", QUEUE_NAME,
         queue_declare->message_count);

  // 5. Configurar QoS (procesar 1 mensaje a la vez)
  amqp_basic_qos(conn,
                 1, // canal
                 0, // prefetch_size
                 1, // prefetch_count (1 mensaje a la vez)
                 0  // global
  );
  reply = amqp_get_rpc_reply(conn);
  if (check_amqp_error(reply, "Configurar QoS")) {
    amqp_channel_close(conn, 1, AMQP_REPLY_SUCCESS);
    amqp_connection_close(conn, AMQP_REPLY_SUCCESS);
    amqp_destroy_connection(conn);
    return 1;
  }

  // 6. Consumir mensajes
  amqp_basic_consume(conn,
                     1, // canal
                     amqp_cstring_bytes(QUEUE_NAME),
                     amqp_empty_bytes, // consumer_tag
                     0,                // no_local
                     0,                // no_ack (mandaremos ACK manual)
                     0,                // exclusive
                     amqp_empty_table);
  reply = amqp_get_rpc_reply(conn);
  if (check_amqp_error(reply, "Iniciar consumo")) {
    amqp_channel_close(conn, 1, AMQP_REPLY_SUCCESS);
    amqp_connection_close(conn, AMQP_REPLY_SUCCESS);
    amqp_destroy_connection(conn);
    return 1;
  }

  printf("\n✅ Consumer iniciado exitosamente\n");
  printf("👂 Escuchando mensajes de la cola '%s'...\n", QUEUE_NAME);
  printf("   (Presiona Ctrl+C para detener)\n\n");
  fflush(stdout);

  // 7. Loop infinito: Esperar y procesar mensajes
  while (1) {
    amqp_envelope_t envelope;
    amqp_maybe_release_buffers(conn);

    // Esperar mensaje (timeout de 1 segundo para permitir señales)
    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;

    reply = amqp_consume_message(conn, &envelope, &timeout, 0);

    if (reply.reply_type == AMQP_RESPONSE_LIBRARY_EXCEPTION) {
      if (reply.library_error == AMQP_STATUS_TIMEOUT) {
        // Timeout normal, continuar esperando
        continue;
      }
      if (reply.library_error == AMQP_STATUS_UNEXPECTED_STATE) {
        // Puede ocurrir durante shutdown, continuar
        continue;
      }
      fprintf(stderr, "❌ Error al consumir mensaje: %s\n",
              amqp_error_string2(reply.library_error));
      break;
    }

    if (reply.reply_type != AMQP_RESPONSE_NORMAL) {
      if (check_amqp_error(reply, "Consumir mensaje")) {
        break;
      }
      continue;
    }

    // Procesar mensaje recibido
    process_message((const char *)envelope.message.body.bytes,
                    envelope.message.body.len);

    // Enviar ACK (confirmar que procesamos el mensaje)
    amqp_basic_ack(conn, 1, envelope.delivery_tag, 0);

    // Liberar memoria del envelope
    amqp_destroy_envelope(&envelope);
  }

  // 8. Cleanup (solo se alcanza si hay error o señal de parada)
  printf("\n🛑 Cerrando consumer...\n");
  amqp_channel_close(conn, 1, AMQP_REPLY_SUCCESS);
  amqp_connection_close(conn, AMQP_REPLY_SUCCESS);
  amqp_destroy_connection(conn);
  printf("✅ Consumer cerrado correctamente\n");

  return 0;
}
