import { defineConfig } from "astro/config";
import starlight from "@astrojs/starlight";

const is_github_actions = process.env.GITHUB_ACTIONS === "true";
const repository = process.env.GITHUB_REPOSITORY || "";
const repository_owner = process.env.GITHUB_REPOSITORY_OWNER || "";
const repository_name = repository.includes("/") ? repository.split("/")[1] : "";
const site = repository_owner ? `https://${repository_owner}.github.io` : undefined;
const base = is_github_actions && repository_name ? `/${repository_name}` : undefined;

export default defineConfig({
  site,
  base,
  integrations: [
    starlight({
      title: "Thagore Developer Docs",
      description: "Code-truth documentation for Thagore compiler, language, runtime, and ecosystem.",
      sidebar: [
        {
          label: "Start Here",
          autogenerate: { directory: "." }
        }
      ]
    })
  ]
});
