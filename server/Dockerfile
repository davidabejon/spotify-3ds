# Imagen base ligera
FROM python:3.11-slim

# Evita archivos .pyc y buffers raros
ENV PYTHONDONTWRITEBYTECODE=1
ENV PYTHONUNBUFFERED=1

# Carpeta de trabajo dentro del contenedor
WORKDIR /app

# Instalar dependencias
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

# Copiar el código
COPY . .

# Puerto donde corre FastAPI
EXPOSE 8000

# Comando para arrancar el server
CMD ["uvicorn", "server:app", "--host", "0.0.0.0", "--port", "8000", "--reload", "--reload-include", "spotify_config.json"]
