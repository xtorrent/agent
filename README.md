# agent
xtorrent agent

### compile

```
docker pull csuliuming/gko3-compile-env:v1.0
docker run -ti -name xtorrent csuliuming/gko3-compile-env:v1.0 bash
git clone https://github.com/xtorrent/agent.git
cd agent
./build.sh                # generate gko3-agent.tgz in $PWD/output/
```

### run
- must set `JAVA_HOME`
- if you need to download from HDFS
 
    ```
    LD_LIBRARY_PATH=lib:${JAVA_HOME}/jre/lib/amd64/server:${LD_LIBRARY_PATH}  gko3 down -C hdfs://username@host/path/to/file
    ```