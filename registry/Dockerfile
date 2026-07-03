FROM python:3.12-slim

WORKDIR /app

COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY . .

RUN mkdir -p data

ENV REGISTRY_DATA_FILE=data/registry.json
ENV REGISTRY_TOKEN=""
ENV REGISTRY_TITLE="zero-pm registry"

EXPOSE 8000

CMD ["uvicorn", "main:app", "--host", "0.0.0.0", "--port", "8000"]
