# Install on a Synology NAS

Use Container Manager to build and run CinemaSeat Audio Decoder 0.1.6 on the NAS.

1. Save the raw [docker-compose.yml](../docker/synology/docker-compose.yml)
   and [Dockerfile](../docker/synology/Dockerfile) from GitHub. Upload both
   files to the same project folder using File Station, for example
   `docker/cinemaseat-service`. Keep the filenames exactly as shown.
2. In **Container Manager → Project → Create**, name the project
   `cinemaseat-service` and select that folder as its path. Choose to upload a
   Compose file and select the downloaded `docker-compose.yml`.
3. Finish the wizard and choose to start the project. If the project has not
   been built, select **Action → Build** and then **Start**.
4. Open `http://<NAS-IP>:9078/healthz` in a browser. The service should return
   JSON containing `"protocol":1` and `"status":"ok"`. Its WebSocket endpoint
   is `ws://<NAS-IP>:9078`.

The build downloads the tagged release source from the public
[Ahnl66/CinemaSeatService](https://github.com/Ahnl66/CinemaSeatService)
repository and compiles the service on the NAS. The NAS needs internet access
during the build. The image includes the default settings and reads them at
startup. Keep TCP port 9078 on a trusted LAN because the service has no
authentication or TLS.

To update, save the latest two Synology files to the project folder and
redeploy with the updated YAML in **Project → Details → YAML Configurations**.
Check the health endpoint after the project restarts.
