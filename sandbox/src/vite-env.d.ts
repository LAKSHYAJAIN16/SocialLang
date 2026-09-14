/// <reference types="vite/client" />

declare module "*.sl?raw" {
  const content: string;
  export default content;
}
