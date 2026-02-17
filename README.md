# DVP - Distributed Video Processing System

Sistema distribuido para procesamiento de video utilizando MPI (Message Passing Interface) con una arquitectura basada en microservicios.

## 📋 Tabla de Contenidos

- [Arquitectura del Sistema](#-arquitectura-del-sistema)
- [Requisitos](#-requisitos)
- [Setup en Windows](#-setup-en-windows)
- [Setup en Linux](#-setup-en-linux)
- [Flujo de Trabajo](#-flujo-de-trabajo)
- [Arquitectura de Red](#-arquitectura-de-red)
- [Servicios Disponibles](#-servicios-disponibles)
- [Consumer de RabbitMQ](#-consumer-de-rabbitmq-nodo-maestro)
- [Testing del Sistema](#-testing-del-sistema)
- [Validación del Sistema](#-validación-del-sistema)
- [Troubleshooting](#-troubleshooting)

## 🏗️ Arquitectura del Sistema

El sistema DVP está compuesto por los siguientes componentes:

```
┌─────────────────────────────────────────────────────────────┐
│                        DVP System                            │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  ┌──────────┐                                               │
│  │   API    │  ◄─── FastAPI REST Service                    │
│  │  (8000)  │       - Job Management                        │
│  └────┬─────┘       - Publish to RabbitMQ                   │
│       │                                                      │
│       ├──────────┬──────────┬──────────┬──────────┐         │
│       ▼          ▼          ▼          ▼          ▼         │
│  ┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐   │
│  │ MPI    │ │RabbitMQ│ │Postgres│ │ MinIO  │ │  SSH   │   │
│  │ Cluster│ │ (5672) │ │ (5432) │ │ (9000) │ │  Keys  │   │
│  └────────┘ └───┬────┘ └────────┘ └────────┘ └────────┘   │
│      │          │ Queue: video_jobs                         │
│      │          │                                           │
│      │          ▼                                           │
│      │      ┌──────┐  ◄─── RabbitMQ Consumer (C)           │
│      │      │Master│       - Escucha cola video_jobs       │
│      │      └──┬───┘       - Parsea JSON                   │
│      │         │            - Invoca mpirun                 │
│      ├─────────┴────┬────────┐                             │
│      ▼              ▼        ▼                              │
│  ┌──────┐       ┌───────┐ ┌───────┐                        │
│  │Master│       │Worker1│ │Worker2│  ◄─── MPI Workers      │
│  └──────┘       └───────┘ └───────┘                        │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

### Componentes:

- **API**: Servicio REST basado en FastAPI que gestiona jobs y publica trabajos a RabbitMQ
- **MPI Cluster**: Clúster de 3 nodos (1 master + 2 workers) para procesamiento paralelo
- **RabbitMQ**: Cola de mensajes para comunicación asíncrona entre API y nodo maestro
  - **Consumer (Master)**: Proceso en C que escucha la cola `video_jobs`, parsea mensajes JSON y ejecuta trabajos MPI
- **PostgreSQL**: Base de datos para almacenar estado de jobs
- **MinIO**: Almacenamiento S3-compatible para artifacts y archivos procesados
- **SSH Keys**: Sistema de claves compartidas para comunicación segura entre nodos MPI

## 📦 Requisitos

### Windows
- Docker Desktop 4.0+
- Git Bash o WSL2
- 8GB RAM mínimo (recomendado 16GB)
- 10GB espacio en disco

### Linux
- Docker Engine 20.10+
- Docker Compose 2.0+
- 8GB RAM mínimo (recomendado 16GB)
- 10GB espacio en disco

## 🪟 Setup en Windows

### 1. Instalar Docker Desktop

Descarga e instala [Docker Desktop](https://www.docker.com/products/docker-desktop/) para Windows.

### 2. Clonar el Repositorio

```bash
git clone <repository-url>
cd dvp
```

### 3. Configurar Line Endings

Git Bash automáticamente configura line endings, pero verifica:

```bash
git config core.autocrlf true
```

### 4. Build y Levantar Containers

```bash
# Desde Git Bash o PowerShell
docker-compose build
docker-compose up -d
```

### 5. Verificar el Sistema

```bash
# Usando Git Bash
bash validate-setup.sh

# O usando Docker Desktop PowerShell
sh validate-setup.sh
```

### Acceso a los Servicios

- **API Docs**: http://localhost:8000/docs
- **MinIO Console**: http://localhost:9001 (user: `minio`, password: `minio123`)
- **RabbitMQ Management**: http://localhost:15672 (user: `guest`, password: `guest`)

## 🐧 Setup en Linux

### 1. Instalar Docker y Docker Compose

```bash
# Ubuntu/Debian
sudo apt-get update
sudo apt-get install -y docker.io docker-compose

# Iniciar servicio Docker
sudo systemctl start docker
sudo systemctl enable docker

# Agregar usuario al grupo docker (opcional)
sudo usermod -aG docker $USER
newgrp docker
```

### 2. Clonar el Repositorio

```bash
git clone <repository-url>
cd dvp
```

### 3. Build y Levantar Containers

```bash
docker-compose build
docker-compose up -d
```

### 4. Verificar el Sistema

```bash
bash validate-setup.sh
```

### Acceso a los Servicios

- **API Docs**: http://localhost:8000/docs
- **MinIO Console**: http://localhost:9001 (user: `minio`, password: `minio123`)
- **RabbitMQ Management**: http://localhost:15672 (user: `guest`, password: `guest`)

## 🔄 Flujo de Trabajo

### Proceso de Buildeo e Inicio

```
1. ssh-keygen (init)
   │
   ├─► Genera claves SSH compartidas
   └─► Almacena en volumen ssh-keys
        │
        ▼
2. Build de Imágenes
   │
   ├─► mpi/* → Imagen MPI (Ubuntu + OpenMPI)
   └─► api/* → Imagen API (Python + FastAPI)
        │
        ▼
3. Inicio de Servicios Base
   │
   ├─► PostgreSQL (base de datos)
   ├─► RabbitMQ (cola de mensajes)
   └─► MinIO (almacenamiento)
        │
        ▼
4. Inicio del Cluster MPI
   │
   ├─► mpi-master (nodo maestro)
   ├─► mpi-worker1 (nodo trabajador 1)
   └─► mpi-worker2 (nodo trabajador 2)
        │
        └─► Copian claves SSH desde volumen compartido
        └─► Inician servicio SSH
        └─► Configuran autenticación sin contraseña
        └─► Master inicia RabbitMQ Consumer en background
             │
             ▼
5. Inicio de API
   │
   └─► Conecta a todos los servicios
   └─► Valida conectividad MPI
   └─► Expone endpoints REST
        │
        ▼
6. Sistema Listo ✓
```

### Comandos Útiles

```bash
# Ver logs de todos los servicios
docker-compose logs -f

# Ver logs de un servicio específico
docker-compose logs -f api

# Ver logs del consumer de RabbitMQ (nodo maestro)
docker exec mpi-master cat /var/log/rabbitmq_consumer.log

# Ver logs en tiempo real del consumer
docker exec mpi-master tail -f /var/log/rabbitmq_consumer.log

# Verificar que el consumer está corriendo
docker exec mpi-master ps aux | grep rabbitmq_consumer

# Reiniciar un servicio
docker-compose restart api

# Reiniciar el nodo maestro (reinicia el consumer)
docker-compose restart mpi-master

# Acceder al master MPI
docker exec -u mpiuser -it mpi-master bash

# Ejecutar comando MPI manual
docker exec -u mpiuser mpi-master mpirun --hostfile /home/mpiuser/hostfile -np 6 hostname

# Verificar estado de colas en RabbitMQ
docker exec rabbitmq rabbitmqadmin list queues name messages consumers

# Detener todos los servicios(elimina)
docker-compose down

# Detener todos los servicios(no elimina)
docker-compose stop

# Iniciar todos los servicios (después de stop)
docker-compose start

# Rebuild completo (limpieza total)
docker-compose down -v
docker-compose build --no-cache
docker-compose up -d
```

## 🌐 Arquitectura de Red

### Red: `my_net` (Bridge Network)

Todos los servicios se comunican a través de una red bridge privada:

```
my_net (172.x.x.x/16)
│
├─── mpi-master (hostname: master)
│    └─► SSH: 22
│    └─► MPI Communication
│
├─── mpi-worker1 (hostname: worker1)
│    └─► SSH: 22
│    └─► MPI Communication
│
├─── mpi-worker2 (hostname: worker2)
│    └─► SSH: 22
│    └─► MPI Communication
│
├─── api
│    └─► HTTP: 8000 (expuesto)
│    └─► SSH Client (para MPI)
│
├─── rabbitmq
│    └─► AMQP: 5672 (expuesto)
│    └─► Management: 15672 (expuesto)
│
├─── postgres
│    └─► PostgreSQL: 5432 (expuesto)
│
└─── minio
     └─► S3 API: 9000 (expuesto)
     └─► Console: 9001 (expuesto)
```

### Comunicación SSH entre Nodos MPI

Los nodos MPI utilizan autenticación SSH basada en claves:

1. `ssh-keygen` genera un par de claves RSA al inicio
2. Las claves se almacenan en un volumen Docker compartido (`ssh-keys`)
3. Cada nodo MPI copia las claves a `/home/mpiuser/.ssh/`
4. La configuración SSH permite conexiones sin verificación de host
5. El usuario `mpiuser` puede ejecutar comandos en cualquier nodo sin contraseña

## 🛠️ Servicios Disponibles

| Servicio | Puerto | Credenciales | Descripción |
|----------|--------|--------------|-------------|
| API | 8000 | N/A | REST API para gestión de jobs |
| PostgreSQL | 5432 | `dbuser` / `AMyGOUcgJJk7YjA6a8cS` | Base de datos |
| RabbitMQ | 5672, 15672 | `guest` / `guest` | Cola de mensajes |
| MinIO | 9000, 9001 | `minio` / `minio123` | Almacenamiento S3 |
| MPI Master | - | `mpiuser` (SSH key) | Nodo maestro MPI |
| MPI Worker 1 | - | `mpiuser` (SSH key) | Nodo trabajador MPI |
| MPI Worker 2 | - | `mpiuser` (SSH key) | Nodo trabajador MPI |

## 🔄 Consumer de RabbitMQ (Nodo Maestro)

El nodo maestro incluye un **consumer de RabbitMQ** escrito en C que se inicia automáticamente al arrancar el contenedor.

### Funcionamiento

- **Cola escuchada**: `video_jobs`
- **Formato de mensajes**: JSON (ver [MENSAJE_RABBITMQ_CONTRACT.md](MENSAJE_RABBITMQ_CONTRACT.md))
- **Proceso**: Se ejecuta en background (PID visible en logs de inicio)
- **Logs**: `/var/log/rabbitmq_consumer.log` dentro del contenedor master

### Verificar que el Consumer está Corriendo

```bash
# Ver el proceso del consumer
docker exec mpi-master ps aux | grep rabbitmq_consumer

# Ver logs de inicio del consumer
docker logs mpi-master | grep -A 10 "RabbitMQ consumer"
```

### Ver Logs del Consumer

```bash
# Log completo
docker exec mpi-master cat /var/log/rabbitmq_consumer.log

# En tiempo real (seguir nuevos mensajes)
docker exec mpi-master tail -f /var/log/rabbitmq_consumer.log
```

### Salida Esperada del Consumer

```
🚀 Iniciando RabbitMQ Consumer para MPI Master
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
📡 Host: rabbitmq:5672
👤 Usuario: guest
📬 Cola: video_jobs
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

🔌 Conectando a RabbitMQ...
✅ Conectado al servidor
✅ Login exitoso
✅ Canal abierto
✅ Cola 'video_jobs' declarada (mensajes en cola: 0)
✅ Consumer iniciado exitosamente
👂 Escuchando mensajes de la cola 'video_jobs'...
```

### Cuando Recibe un Mensaje

El consumer parsea el JSON y muestra:

```
========================================
📨 MENSAJE RECIBIDO DE LA COLA
========================================
Contenido: {"job_id":"123","video_path":"uploads/video.mp4","task":"convert",...}
Longitud: 95 bytes
========================================

📋 Información del Job:
   Job ID: 123
   Video Path: uploads/video.mp4
   Task: convert
   Params: {"output_format":"webm"}

🚀 Comando MPI que se ejecutaría:
   mpirun -np 6 --hostfile /home/mpiuser/hostfile \
          /home/mpiuser/project/process_video \
          123 uploads/video.mp4 convert

✅ Mensaje procesado
```

## 🧪 Testing del Sistema

### Probar el Consumer de RabbitMQ

Puedes enviar mensajes de prueba manualmente usando RabbitMQ Management UI:

#### 1. Acceder a RabbitMQ Management

- URL: http://localhost:15672
- Usuario: `guest`
- Contraseña: `guest`

#### 2. Publicar un Mensaje de Prueba

1. Ir a **Queues and Streams** → Click en `video_jobs`
2. Bajar a la sección **Publish message**
3. Configurar:
   - **Delivery mode**: `2 - Persistent`
   - **Properties** → **content_type**: `application/json`
4. En **Payload**, pegar:

```json
{
  "job_id": "test_12345",
  "video_path": "uploads/test_video.mp4",
  "task": "convert",
  "params": {
    "output_format": "webm",
    "resolution": "720p"
  }
}
```

5. Click en **Publish message**

#### 3. Verificar que el Consumer Recibió el Mensaje

```bash
# Ver el log del consumer
docker exec mpi-master cat /var/log/rabbitmq_consumer.log
```

Deberías ver el mensaje parseado con toda la información del job.

### Contrato de Mensajes

Para más detalles sobre el formato de mensajes JSON que acepta el consumer, consulta:

📄 **[MENSAJE_RABBITMQ_CONTRACT.md](MENSAJE_RABBITMQ_CONTRACT.md)**

Este archivo incluye:
- Formato completo del JSON
- Ejemplos para diferentes tipos de tareas
- Código Python para publicar desde la API
- Validaciones que realiza el consumer

## ✅ Validación del Sistema

El script `validate-setup.sh` verifica:

1. **Containers**: Todos los servicios están corriendo
2. **SSH Keys**: Claves generadas y copiadas correctamente
3. **SSH Connectivity**: Comunicación entre nodos MPI
4. **API Endpoints**: Conexiones a DB, Storage, Queue y MPI
5. **MPI Execution**: Ejecución de comandos distribuidos

### Salida Esperada

```
==========================================
DVP System Validation Script
==========================================

Step 1: Checking Docker Containers
------------------------------------------
Checking MPI Master Node... ✓ RUNNING
Checking MPI Worker 1... ✓ RUNNING
Checking MPI Worker 2... ✓ RUNNING
Checking API Server... ✓ RUNNING
Checking PostgreSQL Database... ✓ RUNNING
Checking MinIO Storage... ✓ RUNNING
Checking RabbitMQ Queue... ✓ RUNNING

Step 2: Checking SSH Keys
------------------------------------------
Checking SSH keys in MPI Master... ✓ FOUND
Checking SSH keys in MPI Worker 1... ✓ FOUND
Checking SSH keys in MPI Worker 2... ✓ FOUND
Checking SSH keys in API Container... ✓ FOUND

Step 3: Testing SSH Connectivity
------------------------------------------
Testing Master to Master (localhost)... ✓ CONNECTED
Testing Master to Worker 1... ✓ CONNECTED
Testing Master to Worker 2... ✓ CONNECTED
Testing Worker 1 to Master... ✓ CONNECTED
Testing Worker 2 to Master... ✓ CONNECTED

Step 4: Testing API Endpoints
------------------------------------------
Testing Database Connection... ✓ PASSED
Testing Storage Connection... ✓ PASSED
Testing Queue Connection... ✓ PASSED
Testing MPI Connection... ✓ PASSED

Step 5: Testing MPI Execution
------------------------------------------
Testing MPI hostname command... ✓ PASSED (6 processes)

==========================================
Validation Summary
==========================================
Passed: 26
Failed: 0

✓ All checks passed! System is ready.
```

## 🔧 Troubleshooting

### Problema: Containers no inician

```bash
# Ver logs
docker-compose logs -f

# Rebuild completo
docker-compose down -v
docker-compose build --no-cache
docker-compose up -d
```

### Problema: SSH Keys no se generan

```bash
# Eliminar volumen y regenerar
docker-compose down
docker volume rm dvp_ssh-keys
docker-compose up -d
```

### Problema: API no conecta a MPI

```bash
# Verificar logs de API
docker-compose logs api

# Rebuild solo API
docker-compose build api
docker-compose up -d api
```

### Problema: Permisos en Windows

Asegúrate de ejecutar comandos desde Git Bash con:

```bash
export MSYS_NO_PATHCONV=1
```

### Problema: MinIO no accesible

```bash
# Verificar que el bucket existe
docker exec minio ls /data/

# Recrear bucket si es necesario
docker exec minio mkdir -p /data/artifacts
```

### Problema: Consumer de RabbitMQ no está corriendo

```bash
# Verificar si el proceso está activo
docker exec mpi-master ps aux | grep rabbitmq_consumer

# Ver logs del container para ver errores de inicio
docker logs mpi-master

# Reiniciar el nodo maestro
docker-compose restart mpi-master

# Verificar logs del consumer
docker exec mpi-master cat /var/log/rabbitmq_consumer.log
```

### Problema: Mensajes no se procesan

```bash
# 1. Verificar que solo hay 1 consumer conectado
docker exec rabbitmq rabbitmqadmin list queues name messages consumers

# Si hay más de 1 consumer, reiniciar todo:
docker-compose down
docker-compose up -d

# 2. Verificar que RabbitMQ está accesible
docker exec mpi-master nc -zv rabbitmq 5672

# 3. Ver si hay mensajes en cola
docker exec rabbitmq rabbitmqadmin list queues
```

### Problema: Consumer muestra errores de conexión

```bash
# Verificar que RabbitMQ esté completamente iniciado
docker-compose logs rabbitmq

# Esperar unos segundos y reiniciar el master
sleep 10
docker-compose restart mpi-master
```

## 📚 Desarrollo

### Estructura del Proyecto

```
dvp/
├── api/                          # API Service
│   ├── src/                     # Source code
│   ├── Dockerfile
│   ├── entrypoint.sh
│   └── pyproject.toml
├── mpi/                         # MPI Cluster
│   ├── src/                     # Source code
│   │   ├── process_video.c      # Código de procesamiento de video en C
│   │   └── rabbitmq_consumer.c  # Consumer de RabbitMQ en C
│   ├── Dockerfile
│   └── init-ssh.sh
├── docker-compose.yml           # Orquestación de servicios
├── validate-setup.sh            # Script de validación
└── MENSAJE_RABBITMQ_CONTRACT.md # Contrato de mensajes API ↔ Master
```

### Variables de Entorno

Las variables de entorno están configuradas en `docker-compose.yml`:

- **S3_ACCESS_KEY_ID**: Credenciales de MinIO
- **S3_SECRET_ACCESS_KEY**: Credenciales de MinIO
- **POSTGRES_USER/PASSWORD/DB**: Configuración de PostgreSQL
- **RMQ_HOST/PORT/USER/PASSWORD**: Configuración de RabbitMQ
- **MPI_MASTER_HOST**: Hostname del nodo maestro MPI

## 📄 Licencia

[Especificar licencia del proyecto]
