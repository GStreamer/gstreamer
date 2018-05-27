FROM python:3

RUN pip3 install --user websockets

WORKDIR /opt/
COPY . /opt/

RUN sed -i 's/sslctx.load_cert_chain(chain_pem, keyfile=key_pem)/pass/g' \
    ./simple-server.py
RUN sed -i 's/ssl=sslctx,//g' \
    ./simple-server.py

CMD python -u ./simple-server.py
