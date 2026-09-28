FROM python:3.11-alpine

WORKDIR /app

RUN apk add --no-cache tzdata
ENV TZ=Asia/Ho_Chi_Minh

COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY bridge.py .

CMD ["python", "-u", "bridge.py"]
