FROM node:20-alpine
RUN apk add --no-cache ffmpeg
WORKDIR /app
COPY package.json package-lock.json* ./
RUN npm install --omit=dev --no-audit --no-fund
COPY src ./src
COPY public ./public
RUN mkdir -p data
ENV NODE_ENV=production
EXPOSE 3200
HEALTHCHECK --interval=30s --timeout=5s --retries=3 \
  CMD wget -qO- http://localhost:3200/api/status || exit 1
CMD ["node", "src/index.js"]
