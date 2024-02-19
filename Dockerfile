FROM python:3

WORKDIR ./

# install requirements
COPY requirements.txt ./
RUN pip install --upgrade pip
# RUN pip install --no-cache-dir -r requirements.txt
RUN sed 's/==.*$//' requirements.txt | xargs pip install

COPY . .

CMD [ "python", "./sluckman42.py" ]

# for debugging only
#ENTRYPOINT ["tail"]
#CMD ["-f","/dev/null"]

