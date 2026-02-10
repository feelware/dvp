# Contrato de Mensajes RabbitMQ - API ↔ Nodo Maestro MPI

Este documento define el formato de mensajes que deben intercambiarse entre la API y el Nodo Maestro a través de RabbitMQ.

## 📋 Configuración General

### Cola (Queue)
- **Nombre**: `video_jobs`
- **Durable**: `true` (los mensajes persisten si RabbitMQ se reinicia)
- **Auto-delete**: `false`

### Conexión RabbitMQ
Las siguientes variables de entorno están configuradas en `docker-compose.yml`:

```
RMQ_HOST=rabbitmq
RMQ_PORT=5672
RMQ_USER=guest
RMQ_PASSWORD=guest
```

---

## 📤 Formato del Mensaje JSON

### Estructura

```json
{
  "job_id": "string (obligatorio)",
  "video_path": "string (obligatorio)",
  "task": "string (obligatorio)",
  "params": {
    "key": "value (opcional)"
  }
}
```

### Campos

| Campo | Tipo | Obligatorio | Descripción |
|-------|------|-------------|-------------|
| `job_id` | string | ✅ Sí | ID único del trabajo (generado por la API, usado para tracking en BD) |
| `video_path` | string | ✅ Sí | Ruta del video en MinIO (formato: `bucket/filename`, ej: `uploads/video_12345.mp4`) |
| `task` | string | ✅ Sí | Tipo de tarea: `convert`, `resize`, `cut`, `compress`, etc. |
| `params` | object | ❌ No | Parámetros adicionales específicos de la tarea |

### Parámetros por Tipo de Tarea

#### Task: `convert`
```json
{
  "job_id": "12345",
  "video_path": "uploads/video_12345.mp4",
  "task": "convert",
  "params": {
    "output_format": "webm",
    "codec": "vp9"
  }
}
```

#### Task: `resize`
```json
{
  "job_id": "12346",
  "video_path": "uploads/video_12346.mp4",
  "task": "resize",
  "params": {
    "resolution": "720p",
    "width": 1280,
    "height": 720
  }
}
```

#### Task: `cut`
```json
{
  "job_id": "12347",
  "video_path": "uploads/video_12347.mp4",
  "task": "cut",
  "params": {
    "start_time": "00:00:10",
    "end_time": "00:01:30"
  }
}
```

---

## 🐍 Código Python para la API

### Instalación de Dependencias

Agregar a `api/pyproject.toml`:

```toml
[tool.poetry.dependencies]
pika = "^1.3.0"
```

O con pip:
```bash
pip install pika
```

### Ejemplo de Publisher

```python
# api/src/rabbitmq_publisher.py

import pika
import json
import os
from typing import Optional, Dict

class RabbitMQPublisher:
    """
    Cliente para publicar mensajes de video jobs a RabbitMQ
    """
    
    def __init__(self):
        self.host = os.getenv('RMQ_HOST', 'rabbitmq')
        self.port = int(os.getenv('RMQ_PORT', 5672))
        self.user = os.getenv('RMQ_USER', 'guest')
        self.password = os.getenv('RMQ_PASSWORD', 'guest')
        self.queue_name = 'video_jobs'
    
    def publish_video_job(
        self, 
        job_id: str, 
        video_path: str, 
        task: str, 
        params: Optional[Dict] = None
    ) -> bool:
        """
        Publica un job de video a la cola
        
        Args:
            job_id: ID único del trabajo
            video_path: Ruta del video en MinIO (ej: "uploads/video_123.mp4")
            task: Tipo de tarea ("convert", "resize", "cut", etc.)
            params: Parámetros adicionales (opcional)
        
        Returns:
            bool: True si se publicó exitosamente
        """
        try:
            # Crear conexión
            credentials = pika.PlainCredentials(self.user, self.password)
            connection = pika.BlockingConnection(
                pika.ConnectionParameters(
                    host=self.host,
                    port=self.port,
                    credentials=credentials
                )
            )
            channel = connection.channel()
            
            # Declarar la cola (debe ser durable)
            channel.queue_declare(queue=self.queue_name, durable=True)
            
            # Crear mensaje JSON
            message = {
                "job_id": job_id,
                "video_path": video_path,
                "task": task,
                "params": params or {}
            }
            
            # Publicar mensaje
            channel.basic_publish(
                exchange='',
                routing_key=self.queue_name,
                body=json.dumps(message),
                properties=pika.BasicProperties(
                    delivery_mode=2,  # Mensaje persistente
                    content_type='application/json'
                )
            )
            
            print(f"✅ Mensaje publicado - Job ID: {job_id}")
            
            # Cerrar conexión
            connection.close()
            return True
            
        except Exception as e:
            print(f"❌ Error publicando mensaje: {e}")
            return False


# Uso en endpoint de FastAPI
publisher = RabbitMQPublisher()

@app.post("/api/jobs/upload")
async def upload_video(
    file: UploadFile,
    task: str = "convert",
    output_format: str = "webm"
):
    """
    Endpoint para subir video y crear job de procesamiento
    """
    try:
        # 1. Guardar video en MinIO
        video_path = await save_to_minio(file)  # ej: "uploads/video_12345.mp4"
        
        # 2. Crear registro en PostgreSQL
        job_id = create_job_in_database(
            video_path=video_path,
            task=task,
            status="queued"
        )
        
        # 3. Publicar job a RabbitMQ
        success = publisher.publish_video_job(
            job_id=job_id,
            video_path=video_path,
            task=task,
            params={"output_format": output_format}
        )
        
        if not success:
            update_job_status(job_id, "failed")
            return {"error": "Failed to queue job"}
        
        # 4. Retornar respuesta
        return {
            "job_id": job_id,
            "status": "queued",
            "video_path": video_path,
            "message": "Video uploaded and queued for processing"
        }
        
    except Exception as e:
        return {"error": str(e)}
```

---

## 🔍 Validación del Mensaje

El consumer valida que:
1. ✅ El mensaje sea JSON válido
2. ✅ Contenga los campos obligatorios: `job_id`, `video_path`, `task`
3. ✅ Los campos sean de tipo string (no null, no números)

Si falta algún campo o el JSON es inválido, el consumer rechazará el mensaje y lo imprimirá en los logs.

---

## 🧪 Cómo Probar

### Desde Python (Consola)

```python
from rabbitmq_publisher import RabbitMQPublisher

publisher = RabbitMQPublisher()

# Publicar un mensaje de prueba
publisher.publish_video_job(
    job_id="test_123",
    video_path="uploads/test_video.mp4",
    task="convert",
    params={"output_format": "webm"}
)
```

### Desde RabbitMQ Management UI

1. Ir a http://localhost:15672
2. Login: `guest` / `guest`
3. Ir a **Queues** → `video_jobs`
4. Sección **Publish message**
5. Payload:
```json
{
  "job_id": "manual_test_1",
  "video_path": "uploads/test.mp4",
  "task": "convert",
  "params": {"output_format": "webm"}
}
```
6. Click **Publish message**

Luego verificar los logs del nodo maestro:
```bash
docker logs mpi-master -f
```

---

## 📝 Notas Importantes

1. **Orden de creación**: La API debe crear el registro en la BD ANTES de publicar a RabbitMQ
2. **Manejo de errores**: Si la publicación falla, actualizar el job en BD a estado "failed"
3. **Timeouts**: Configurar timeouts razonables en las conexiones RabbitMQ
4. **Reintentos**: Considerar política de reintentos si RabbitMQ no está disponible
5. **Logging**: Registrar todos los mensajes publicados para debugging

---

## 🔄 Flujo Completo

```
1. Usuario sube video
        ↓
2. API guarda en MinIO → "uploads/video_12345.mp4"
        ↓
3. API crea job en PostgreSQL → job_id="12345", status="queued"
        ↓
4. API publica mensaje a RabbitMQ → cola "video_jobs"
        ↓
5. Consumer (nodo maestro) recibe mensaje
        ↓
6. Consumer parsea JSON y extrae: job_id, video_path, task
        ↓
7. Consumer ejecuta: mpirun -np 6 --hostfile ... process_video job_id video_path task
        ↓
8. MPI procesa video y guarda resultado en MinIO
        ↓
9. MPI notifica completado (actualizar BD o enviar mensaje de vuelta)
        ↓
10. Usuario consulta status y descarga resultado
```

---

## ❓ Preguntas Frecuentes

**Q: ¿Qué pasa si el consumer está caído cuando se publica un mensaje?**
A: El mensaje queda en la cola (es durable) y se procesará cuando el consumer vuelva a estar activo.

**Q: ¿Cómo sabe la API que el video terminó de procesarse?**
A: Hay dos opciones:
- **Polling**: La API consulta el estado en la BD periódicamente
- **Callback**: El nodo maestro publica un mensaje de "completado" a otra cola que la API escucha

**Q: ¿Pueden varios consumers leer de la misma cola?**
A: Sí, RabbitMQ distribuye los mensajes entre múltiples consumers (load balancing automático).

---

## 📧 Contacto

Si hay dudas sobre el formato o necesitas agregar nuevos campos, coordinar con el equipo del nodo maestro.
