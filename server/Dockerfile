FROM python:3.13-slim
WORKDIR /app
COPY server/requirements-maze.txt /app/server/
RUN pip install --no-cache-dir -r /app/server/requirements-maze.txt
COPY server/game_server.py server/import_levels.py server/moderators.py server/admin_dashboard.py server/updater_config.py server/maze_auth.py /app/server/
COPY pages /app/pages
COPY server/web /app/server/web
COPY server/admin /app/server/admin
RUN useradd --uid 10001 --create-home game && mkdir -p /data && chown game:game /data
USER game
ENV HOST=0.0.0.0 PORT=8080 DATABASE_PATH=/data/game.sqlite3
EXPOSE 8080
CMD ["python", "server/game_server.py"]
